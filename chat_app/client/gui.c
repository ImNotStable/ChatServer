#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "gui.h"
#include "net_handler.h"
#include "../common/logger.h"
#include "../common/protocol.h"

// Main window widgets
static GtkWidget *main_window = NULL;
static GtkWidget *chat_view = NULL;
static GtkWidget *message_entry = NULL;
static GtkWidget *send_button = NULL;
static GtkWidget *user_list = NULL;
static GtkListStore *user_list_store = NULL;

// Connect dialog widgets
static GtkWidget *connect_dialog = NULL;
static GtkWidget *server_ip_entry = NULL;

// Nickname dialog widgets
static GtkWidget *nickname_dialog = NULL;
static GtkWidget *nickname_entry = NULL;

// Forward declarations for callbacks
static void on_connect_clicked(GtkButton *button, gpointer user_data);
static void on_send_clicked(GtkButton *button, gpointer user_data);
static void on_message_entry_activate(GtkEntry *entry, gpointer user_data);
static void on_disconnect_clicked(GtkButton *button, gpointer user_data);
static void on_connect_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static void on_main_window_destroy(GtkWidget *widget, gpointer user_data);

// Forward declarations for idle functions
static gboolean gui_add_user_idle(gpointer user_data);
static gboolean gui_remove_user_idle(gpointer user_data);
static gboolean gui_add_system_message_idle(gpointer user_data);
static gboolean gui_add_chat_message_idle(gpointer user_data);

// Callback functions for network events
static void on_nickname_response(NicknameResponse *response);
static void on_chat_message(const ChatMessage *message);
static void on_user_join(UserNotification *notification);
static void on_user_leave(UserNotification *notification);
static void on_user_list(const char *user_list, int length);
static void on_disconnect(void);

// Add this structure after the forward declarations
typedef struct {
    char username[MAX_USERNAME_LEN];
} UserData;

// Flag to track if we're in fallback mode
static int fallback_mode = 0;

// Initialize the GUI
int gui_init(const int *argc, char ***argv) {
    // Initialize GTK
    if (!gtk_init_check((int *) argc, argv)) {
        fprintf(stderr, "Failed to initialize GTK\n");
        return -1;
    }
    
    // Set up network callbacks
    net_handler_set_nickname_callback(on_nickname_response);
    net_handler_set_chat_callback(on_chat_message);
    net_handler_set_user_join_callback(on_user_join);
    net_handler_set_user_leave_callback(on_user_leave);
    net_handler_set_user_list_callback(on_user_list);
    net_handler_set_disconnect_callback(on_disconnect);
    
    // Create the main window
    main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Chat Client");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 800, 600);
    g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_window_destroy), NULL);
    
    // Create a vertical box for the main layout
    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(main_window), main_box);
    
    // Create a toolbar
    GtkWidget *toolbar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 0);
    
    // Add toolbar buttons
    GtkToolItem *connect_button = gtk_tool_button_new(NULL, "Connect");
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(connect_button), "Connect");
    g_signal_connect(connect_button, "clicked", G_CALLBACK(on_connect_clicked), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), connect_button, -1);
    
    GtkToolItem *separator = gtk_separator_tool_item_new();
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), separator, -1);
    
    GtkToolItem *disconnect_button = gtk_tool_button_new(NULL, "Disconnect");
    gtk_tool_button_set_label(GTK_TOOL_BUTTON(disconnect_button), "Disconnect");
    g_signal_connect(disconnect_button, "clicked", G_CALLBACK(on_disconnect_clicked), NULL);
    gtk_toolbar_insert(GTK_TOOLBAR(toolbar), disconnect_button, -1);
    
    // Create a horizontal paned container
    GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), paned, TRUE, TRUE, 0);
    
    // Create a scrolled window for the chat view
    GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_add1(GTK_PANED(paned), chat_scroll);
    
    // Create the chat view (text view)
    chat_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(chat_scroll), chat_view);
    
    // Create a scrolled window for the user list
    GtkWidget *user_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(user_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_add2(GTK_PANED(paned), user_scroll);
    gtk_paned_set_position(GTK_PANED(paned), 600);
    
    // Create the user list
    user_list_store = gtk_list_store_new(1, G_TYPE_STRING);
    user_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(user_list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(user_list), TRUE);
    
    // Add a column to the user list
    GtkTreeViewColumn *column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Online Users");
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);
    
    gtk_tree_view_append_column(GTK_TREE_VIEW(user_list), column);
    gtk_container_add(GTK_CONTAINER(user_scroll), user_list);
    
    // Create a horizontal box for the message entry and send button
    GtkWidget *message_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), message_box, FALSE, FALSE, 0);
    
    // Create the message entry
    message_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(message_entry), "Type your message here");
    g_signal_connect(message_entry, "activate", G_CALLBACK(on_message_entry_activate), NULL);
    gtk_box_pack_start(GTK_BOX(message_box), message_entry, TRUE, TRUE, 0);
    gtk_widget_set_sensitive(message_entry, FALSE);
    
    // Create the send button
    send_button = gtk_button_new_with_label("Send");
    g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(message_box), send_button, FALSE, FALSE, 0);
    gtk_widget_set_sensitive(send_button, FALSE);
    
    // Set up the connect dialog
    connect_dialog = gtk_dialog_new_with_buttons("Connect to Server",
                                                GTK_WINDOW(main_window),
                                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                "Connect", GTK_RESPONSE_ACCEPT,
                                                "Cancel", GTK_RESPONSE_REJECT,
                                                NULL);
    
    GtkWidget *connect_content = gtk_dialog_get_content_area(GTK_DIALOG(connect_dialog));
    
    // Create a grid layout for connect dialog
    GtkWidget *connect_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(connect_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(connect_grid), 5);
    gtk_container_add(GTK_CONTAINER(connect_content), connect_grid);
    
    // Server IP row
    GtkWidget *server_label = gtk_label_new("Server IP:");
    gtk_grid_attach(GTK_GRID(connect_grid), server_label, 0, 0, 1, 1);
    
    server_ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(server_ip_entry), "127.0.0.1");
    gtk_grid_attach(GTK_GRID(connect_grid), server_ip_entry, 1, 0, 1, 1);
    
    // Nickname row
    GtkWidget *nickname_label = gtk_label_new("Nickname:");
    gtk_grid_attach(GTK_GRID(connect_grid), nickname_label, 0, 1, 1, 1);
    
    nickname_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(connect_grid), nickname_entry, 1, 1, 1, 1);
    
    g_signal_connect(connect_dialog, "response", G_CALLBACK(on_connect_dialog_response), NULL);
    
    // We don't need a separate nickname dialog anymore, but keep the variable for compatibility
    nickname_dialog = connect_dialog;
    
    // Show all widgets
    gtk_widget_show_all(main_window);
    
    return 0;
}

// Show the connect dialog
void gui_show_connect_dialog(void) {
    // Make sure the IP is set to a default value
    if (server_ip_entry) {
        gtk_entry_set_text(GTK_ENTRY(server_ip_entry), "127.0.0.1");
    }
    
    // Reset the nickname entry
    if (nickname_entry) {
        gtk_entry_set_text(GTK_ENTRY(nickname_entry), "");
    }
    
    // Show the dialog
    gtk_widget_show_all(connect_dialog);
}

// Show the nickname dialog
void gui_show_nickname_dialog(void) {
    // Reset the nickname entry to avoid stale text
    if (nickname_entry) {
        gtk_entry_set_text(GTK_ENTRY(nickname_entry), "");
    }
    
    // Show the combined connect dialog
    gui_show_connect_dialog();
}

// Show the main chat window
void gui_show_main_window(void) {
    gtk_widget_show(main_window);
}

// Hide the main chat window
void gui_hide_main_window(void) {
    gtk_widget_hide(main_window);
}

// Fix gui_add_chat_message to be safer with text handling
void gui_add_chat_message(const char *username, const char *message) {
    // Check for NULL inputs
    if (!username || !message) {
        return;
    }
    
    // Check if we're on the GTK main thread
    if (!g_main_context_is_owner(g_main_context_default())) {
        // We're not on the main thread, so use g_idle_add
        // Need to store both username and message
        struct ChatMsgData {
            char username[MAX_USERNAME_LEN];
            char message[MAX_MESSAGE_LEN];
        } *data = g_malloc(sizeof(struct ChatMsgData));
        
        // Safely copy the strings
        if (username) {
            strncpy(data->username, username, MAX_USERNAME_LEN - 1);
            data->username[MAX_USERNAME_LEN - 1] = '\0';
        } else {
            data->username[0] = '\0';
        }
        
        if (message) {
            strncpy(data->message, message, MAX_MESSAGE_LEN - 1);
            data->message[MAX_MESSAGE_LEN - 1] = '\0';
        } else {
            data->message[0] = '\0';
        }
        
        g_idle_add((GSourceFunc)gui_add_chat_message_idle, data);
        return;
    }

    // Original function (runs on main thread)
    // Check that the chat view exists
    if (!chat_view) {
        return;
    }
    
    // Get the buffer
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    if (!buffer) {
        return;
    }
    
    // Create tags if they don't exist
    if (!gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "username")) {
        gtk_text_buffer_create_tag(buffer, "username", 
                                 "weight", PANGO_WEIGHT_BOLD, 
                                 "foreground", "blue", NULL);
    }
    
    // Get end iterator
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    
    // Insert the username
    gtk_text_buffer_insert(buffer, &end, "\n", -1);
    
    // Get new end iterator and insert username with tag
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &end, username, -1, "username", NULL);
    
    // Insert the message
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, ": ", -1);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, message, -1);
    
    // Scroll to the end
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chat_view), &end, 0.0, FALSE, 0.0, 0.0);
}

// Fix the system message function similarly
void gui_add_system_message(const char *message) {
    // Check for NULL message
    if (!message) {
        return;
    }
    
    // Check if we're on the GTK main thread
    if (!g_main_context_is_owner(g_main_context_default())) {
        // We're not on the main thread, so use g_idle_add
        char *msg_copy = g_strdup(message);
        g_idle_add((GSourceFunc)gui_add_system_message_idle, msg_copy);
        return;
    }

    // Original function (runs on main thread)
    // Check that chat view exists
    if (!chat_view) {
        return;
    }
    
    // Get the buffer
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    if (!buffer) {
        return;
    }
    
    // Create tags if they don't exist
    if (!gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "system")) {
        gtk_text_buffer_create_tag(buffer, "system", 
                                 "style", PANGO_STYLE_ITALIC, 
                                 "foreground", "gray", NULL);
    }
    
    // Get end iterator
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    
    // Insert the message with tag
    gtk_text_buffer_insert(buffer, &end, "\n", -1);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &end, message, -1, "system", NULL);
    
    // Scroll to the end
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chat_view), &end, 0.0, FALSE, 0.0, 0.0);
}

// Helper function for chat messages
static gboolean gui_add_chat_message_idle(gpointer user_data) {
    struct ChatMsgData {
        char username[MAX_USERNAME_LEN];
        char message[MAX_MESSAGE_LEN];
    } *data = user_data;
    
    // Call the original function (now it will run on main thread)
    gui_add_chat_message(data->username, data->message);
    
    // Free the allocated data
    g_free(data);
    
    // Return FALSE to remove this function from the idle queue
    return FALSE;
}

// Add helper function for system messages
static gboolean gui_add_system_message_idle(gpointer user_data) {
    char *message = (char *)user_data;
    
    // Call the original function (now it will run on main thread)
    gui_add_system_message(message);
    
    // Free the allocated data
    g_free(message);
    
    // Return FALSE to remove this function from the idle queue
    return FALSE;
}

// Update the user list
void gui_update_user_list(const char *user_list, const int length) {
    // Check for null or invalid input
    if (!user_list || length <= 0) {
        logger_log(LOG_WARNING, "Invalid user list received (null or empty)");
        return;
    }
    
    // Validate the buffer contains at least one null-terminated string
    if (user_list[length-1] != '\0') {
        logger_log(LOG_WARNING, "Invalid user list format: missing null terminator");
        return;
    }
    
    // Clear the list store
    gtk_list_store_clear(user_list_store);
    
    // Parse the user list
    const char *p = user_list;
    
    // First string is the header ("Users")
    if (p && *p) {
        logger_log(LOG_DEBUG, "User list header: %s", p);
        
        // Calculate the length of the header to avoid buffer overruns
        size_t header_len = strlen(p);
        if (header_len + 1 >= length) {
            logger_log(LOG_WARNING, "Invalid user list: header too long or missing data");
            return;
        }
        
        // Skip the header, we already have a column header "Online Users"
        // Move to the next string (after the null terminator)
        p += header_len + 1;
        
        // Now parse each username in the list
        int user_count = 0;
        
        while (p < user_list + length && *p) {
            // Safety check: ensure string is null-terminated within the buffer
            const char* next = p;
            size_t remaining = user_list + length - p;
            size_t name_len = 0;
            
            // Find null terminator within remaining buffer
            while (name_len < remaining && next[name_len] != '\0') {
                name_len++;
            }
            
            if (name_len >= remaining) {
                logger_log(LOG_WARNING, "Invalid user list: missing null terminator for username");
                break;
            }
            
            // Add user to the list
            logger_log(LOG_DEBUG, "Adding user to list: %s", p);
            GtkTreeIter iter;
            gtk_list_store_append(user_list_store, &iter);
            gtk_list_store_set(user_list_store, &iter, 0, p, -1);
            
            // Move to the next user
            p += name_len + 1;
            user_count++;
        }
        
        logger_log(LOG_INFO, "Updated user list with %d users", user_count);
    } else {
        logger_log(LOG_WARNING, "Empty user list received");
    }
}

// Add a user to the user list
void gui_add_user(const char *username) {
    // Check if we're on the GTK main thread
    if (!g_main_context_is_owner(g_main_context_default())) {
        // We're not on the main thread, so use g_idle_add
        UserData *data = g_malloc(sizeof(UserData));
        strncpy(data->username, username, MAX_USERNAME_LEN - 1);
        data->username[MAX_USERNAME_LEN - 1] = '\0';
        g_idle_add((GSourceFunc)gui_add_user_idle, data);
        return;
    }

    // The rest of the original function runs on the main thread
    // Check if the user already exists
    GtkTreeModel *model = GTK_TREE_MODEL(user_list_store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    
    while (valid) {
        gchar *name;
        gtk_tree_model_get(model, &iter, 0, &name, -1);
        
        if (strcmp(name, username) == 0) {
            g_free(name);
            return;
        }
        
        g_free(name);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
    
    // Add the user
    gtk_list_store_append(user_list_store, &iter);
    gtk_list_store_set(user_list_store, &iter, 0, username, -1);
}

// Add this helper function after gui_add_user
static gboolean gui_add_user_idle(gpointer user_data) {
    UserData *data = (UserData *)user_data;
    
    // Call the original function (now it will run on main thread)
    gui_add_user(data->username);
    
    // Free the allocated data
    g_free(data);
    
    // Return FALSE to remove this function from the idle queue
    return FALSE;
}

// Remove a user from the user list
void gui_remove_user(const char *username) {
    // Check if we're on the GTK main thread
    if (!g_main_context_is_owner(g_main_context_default())) {
        // We're not on the main thread, so use g_idle_add
        UserData *data = g_malloc(sizeof(UserData));
        strncpy(data->username, username, MAX_USERNAME_LEN - 1);
        data->username[MAX_USERNAME_LEN - 1] = '\0';
        g_idle_add((GSourceFunc)gui_remove_user_idle, data);
        return;
    }

    // Original function runs on main thread
    GtkTreeModel *model = GTK_TREE_MODEL(user_list_store);
    GtkTreeIter iter;
    gboolean valid = gtk_tree_model_get_iter_first(model, &iter);
    
    while (valid) {
        gchar *name;
        gtk_tree_model_get(model, &iter, 0, &name, -1);
        
        if (strcmp(name, username) == 0) {
            gtk_list_store_remove(user_list_store, &iter);
            g_free(name);
            return;
        }
        
        g_free(name);
        valid = gtk_tree_model_iter_next(model, &iter);
    }
}

// Add this helper function after gui_remove_user
static gboolean gui_remove_user_idle(gpointer user_data) {
    UserData *data = (UserData *)user_data;
    
    // Call the original function (now it will run on main thread)
    gui_remove_user(data->username);
    
    // Free the allocated data
    g_free(data);
    
    // Return FALSE to remove this function from the idle queue
    return FALSE;
}

// Clear the chat history
void gui_clear_chat(void) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    gtk_text_buffer_set_text(buffer, "", -1);
}

// Show an error message dialog
void gui_show_error(const char *title, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK,
                                             "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Show an info message dialog
void gui_show_info(const char *title, const char *message) {
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(main_window),
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_INFO,
                                             GTK_BUTTONS_OK,
                                             "%s", message);
    gtk_window_set_title(GTK_WINDOW(dialog), title);
    gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
}

// Run the GUI main loop
void gui_main(void) {
    gtk_main();
}

// Clean up GUI resources
void gui_cleanup(void) {
    // Destroy dialogs
    if (connect_dialog) {
        gtk_widget_destroy(connect_dialog);
        connect_dialog = NULL;
    }
    
    if (nickname_dialog) {
        gtk_widget_destroy(nickname_dialog);
        nickname_dialog = NULL;
    }
    
    // Destroy main window
    if (main_window) {
        gtk_widget_destroy(main_window);
        main_window = NULL;
    }
}

// Connect dialog response handler
static void on_connect_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        const char *ip = gtk_entry_get_text(GTK_ENTRY(server_ip_entry));
        const char *nickname = gtk_entry_get_text(GTK_ENTRY(nickname_entry));
        
        // Check for NULL pointers before using strlen
        if (!ip || strlen(ip) == 0) {
            gui_show_error("Invalid Input", "Server IP cannot be empty.");
            return;
        }
        
        if (!nickname || strlen(nickname) == 0) {
            gui_show_error("Invalid Input", "Nickname cannot be empty.");
            return;
        }
        
        if (strlen(nickname) > MAX_USERNAME_LEN - 1) {
            char error_msg[100];
            snprintf(error_msg, sizeof(error_msg), "Nickname is too long. Maximum length is %d characters.", MAX_USERNAME_LEN - 1);
            gui_show_error("Invalid Input", error_msg);
            return;
        }
        
        // Connect to server and set nickname in one step
        gui_add_system_message("Connecting and setting nickname...");
        if (net_handler_connect_with_nickname(ip, nickname) != 0) {
            gui_add_system_message("Connection failed: Could not connect to server or set nickname.");
            gui_show_error("Connection Error", "Failed to connect to server or set nickname.");
            return;
        }
        
        // Hide connect dialog
        gtk_widget_hide(GTK_WIDGET(dialog));
        
        // Enable message entry and send button
        gtk_widget_set_sensitive(message_entry, TRUE);
        gtk_widget_set_sensitive(send_button, TRUE);
    } else {
        gtk_widget_hide(GTK_WIDGET(dialog));
    }
}

// Send button click handler
static void on_send_clicked(GtkButton *button, gpointer user_data) {
    // Get the text from the message entry
    const char *message = gtk_entry_get_text(GTK_ENTRY(message_entry));
    
    // Safer check for empty message
    if (!message || strlen(message) == 0) {
        return;
    }
    
    // Check if we're connected and have a nickname
    if (!net_handler_is_connected()) {
        gui_show_error("Not Connected", "You must connect to a server first.");
        return;
    }
    
    if (!net_handler_has_nickname()) {
        gui_show_error("No Nickname", "You must set a nickname before sending messages.");
        gui_show_nickname_dialog();
        return;
    }
    
    // Send the message
    if (net_handler_send_message(message) != 0) {
        gui_show_error("Send Error", "Failed to send message.");
        return;
    }
    
    // Clear the message entry
    gtk_entry_set_text(GTK_ENTRY(message_entry), "");
}

// Message entry activation handler (Enter key)
static void on_message_entry_activate(GtkEntry *entry, gpointer user_data) {
    on_send_clicked(GTK_BUTTON(send_button), NULL);
}

// Disconnect button click handler
static void on_disconnect_clicked(GtkButton *button, gpointer user_data) {
    // Disconnect from the server
    net_handler_disconnect();
    
    // Update UI to show we're disconnected
    gui_add_system_message("Disconnected from server.");
    
    // Clear the user list
    gtk_list_store_clear(user_list_store);
    
    // Disable message entry and send button
    gtk_widget_set_sensitive(message_entry, FALSE);
    gtk_widget_set_sensitive(send_button, FALSE);
}

// Main window destroy handler
static void on_main_window_destroy(GtkWidget *widget, gpointer user_data) {
    // Make sure we disconnect cleanly
    net_handler_disconnect();
    
    // Log the shutdown
    fprintf(stderr, "Chat client shutting down...\n");
    
    // Quit the GTK main loop
    gtk_main_quit();
}

// Network event callbacks
static void on_nickname_response(NicknameResponse *response) {
    if (response->status == STATUS_SUCCESS) {
        gui_add_system_message("Nickname set successfully.");
        
        // Enable message entry and send button
        if (message_entry) {
            gtk_widget_set_sensitive(message_entry, TRUE);
        }
        
        if (send_button) {
            gtk_widget_set_sensitive(send_button, TRUE);
        }
    } else {
        char buffer[MAX_MESSAGE_LEN + 64];
        snprintf(buffer, sizeof(buffer), "Connection rejected: %.980s", response->message);
        gui_add_system_message(buffer);
        gui_show_error("Connection Rejected", buffer);
        
        // Connection will be automatically closed by the network handler
        // Show the connect dialog again to allow the user to try a different nickname
        g_idle_add((GSourceFunc)gui_show_connect_dialog, NULL);
    }
}

static void on_chat_message(const ChatMessage *message) {
    gui_add_chat_message(message->username, message->message);
}

static void on_user_join(UserNotification *notification) {
    // Create a user join message
    char buffer[MAX_USERNAME_LEN + 32];
    snprintf(buffer, sizeof(buffer), "%s has joined the chat.", notification->username);
    
    // Add system message (should be made thread-safe)
    gui_add_system_message(buffer);
    
    // Add user to the list (now thread-safe)
    gui_add_user(notification->username);
}

static void on_user_leave(UserNotification *notification) {
    char buffer[MAX_USERNAME_LEN + 32];
    snprintf(buffer, sizeof(buffer), "%s has left the chat.", notification->username);
    gui_add_system_message(buffer);
    
    // Remove user from the list
    gui_remove_user(notification->username);
}

static void on_user_list(const char *user_list, const int length) {
    // Add validation before calling gui_update_user_list
    if (!user_list) {
        logger_log(LOG_WARNING, "Received NULL user list");
        return;
    }
    
    if (length <= 0 || length > MAX_MESSAGE_LEN) {
        logger_log(LOG_WARNING, "Received invalid user list length: %d", length);
        return;
    }
    
    // Log before processing
    logger_log(LOG_DEBUG, "Processing user list message of length %d", length);
    
    gui_update_user_list(user_list, length);
}

// Fix on_disconnect function to better handle disconnect
static void on_disconnect(void) {
    if (!g_main_context_is_owner(g_main_context_default())) {
        // We're not on the main thread, add to idle queue
        g_idle_add((GSourceFunc)on_disconnect, NULL);
        return;
    }

    gui_add_system_message("Disconnected from server.");
    
    // Clear the user list
    if (user_list_store) {
        gtk_list_store_clear(user_list_store);
    }
    
    // Disable message entry and send button
    if (message_entry) {
        gtk_widget_set_sensitive(message_entry, FALSE);
    }
    
    if (send_button) {
        gtk_widget_set_sensitive(send_button, FALSE);
    }
}

// Connect button click handler
static void on_connect_clicked(GtkButton *button, gpointer user_data) {
    if (net_handler_is_connected()) {
        gui_show_info("Already Connected", "You are already connected to a server.");
        return;
    }
    
    // Show the connect dialog
    gui_show_connect_dialog();
}

// Check if we're running in fallback mode
int gui_is_fallback_mode(void) {
    return fallback_mode;
}

// Initialize the fallback (text-only) mode
int gui_init_fallback(void) {
    fallback_mode = 1;
    return 0;
}

// Run the fallback mode main loop
void gui_main_fallback(void) {
    char buffer[MAX_MESSAGE_LEN];

    printf("Chat Client (Fallback Mode)\n");
    printf("==========================\n");
    printf("Commands:\n");
    printf("  /connect <server> <nickname> - Connect to server with nickname\n");
    printf("  /disconnect - Disconnect from server\n");
    printf("  /quit - Exit the program\n");
    printf("==========================\n");

    while (1) {
        printf("> ");
        if (!fgets(buffer, sizeof(buffer), stdin)) {
            break;
        }

        // Remove trailing newline
        size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

        // Process commands
        if (strncmp(buffer, "/quit", 5) == 0) {
            break;
        } else if (strncmp(buffer, "/connect ", 9) == 0) {
            char server[256];
            char nickname[MAX_USERNAME_LEN];
            if (sscanf(buffer + 9, "%255s %31s", server, nickname) == 2) {
                printf("Connecting to %s with nickname %s...\n", server, nickname);
                if (net_handler_connect_with_nickname(server, nickname) != 0) {
                    printf("Failed to connect to server or set nickname.\n");
                }
            } else {
                printf("Usage: /connect <server> <nickname>\n");
            }
        } else if (strncmp(buffer, "/disconnect", 11) == 0) {
            net_handler_disconnect();
            printf("Disconnected from server.\n");
        } else {
            // Send as chat message
            if (net_handler_is_connected() && net_handler_has_nickname()) {
                if (net_handler_send_message(buffer) != 0) {
                    printf("Failed to send message.\n");
                }
            } else {
                printf("Not connected to a server. Use /connect <server> <nickname> first.\n");
            }
        }
    }
}

// Display a chat message in fallback mode
void gui_add_chat_message_fallback(const char *username, const char *message) {
    printf("%s: %s\n", username, message);
}

// Display a system message in fallback mode
void gui_add_system_message_fallback(const char *message) {
    printf("[SYSTEM] %s\n", message);
} 