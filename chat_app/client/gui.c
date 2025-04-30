#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <gtk/gtk.h>
#include "gui.h"
#include "net_handler.h"
#include "../common/logger.h"
#include "../common/protocol.h"

static GtkWidget *main_window = NULL;
static GtkWidget *chat_view = NULL;
static GtkWidget *message_entry = NULL;
static GtkWidget *send_button = NULL;
static GtkWidget *user_list = NULL;
static GtkListStore *user_list_store = NULL;

static GtkWidget *connect_dialog = NULL;
static GtkWidget *server_ip_entry = NULL;

static GtkWidget *nickname_dialog = NULL;
static GtkWidget *nickname_entry = NULL;

static void on_connect_clicked(GtkButton *button, gpointer user_data);
static void on_send_clicked(GtkButton *button, gpointer user_data);
static void on_message_entry_activate(GtkEntry *entry, gpointer user_data);
static void on_disconnect_clicked(GtkButton *button, gpointer user_data);
static void on_connect_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data);
static void on_main_window_destroy(GtkWidget *widget, gpointer user_data);

static gboolean gui_add_user_idle(gpointer user_data);
static gboolean gui_remove_user_idle(gpointer user_data);
static gboolean gui_add_system_message_idle(gpointer user_data);
static gboolean gui_add_chat_message_idle(gpointer user_data);

static void on_nickname_response(NicknameResponse *response);
static void on_chat_message(const ChatMessage *message);
static void on_user_join(UserNotification *notification);
static void on_user_leave(UserNotification *notification);
static void on_user_list(const char *user_list, int length);
static void on_disconnect(void);

typedef struct {
    char username[MAX_USERNAME_LEN];
} UserData;

static int fallback_mode = 0;

int gui_init(const int *argc, char ***argv) {
        if (!gtk_init_check((int *) argc, argv)) {
        fprintf(stderr, "Failed to initialize GTK\n");
        return -1;
    }
    
        net_handler_set_nickname_callback(on_nickname_response);
    net_handler_set_chat_callback(on_chat_message);
    net_handler_set_user_join_callback(on_user_join);
    net_handler_set_user_leave_callback(on_user_leave);
    net_handler_set_user_list_callback(on_user_list);
    net_handler_set_disconnect_callback(on_disconnect);
    
        main_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(main_window), "Chat Client");
    gtk_window_set_default_size(GTK_WINDOW(main_window), 800, 600);
    g_signal_connect(main_window, "destroy", G_CALLBACK(on_main_window_destroy), NULL);
    
        GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    gtk_container_add(GTK_CONTAINER(main_window), main_box);
    
        GtkWidget *toolbar = gtk_toolbar_new();
    gtk_box_pack_start(GTK_BOX(main_box), toolbar, FALSE, FALSE, 0);
    
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
    
        GtkWidget *paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_pack_start(GTK_BOX(main_box), paned, TRUE, TRUE, 0);
    
        GtkWidget *chat_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(chat_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_add1(GTK_PANED(paned), chat_scroll);
    
        chat_view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(chat_view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(chat_view), GTK_WRAP_WORD_CHAR);
    gtk_container_add(GTK_CONTAINER(chat_scroll), chat_view);
    
        GtkWidget *user_scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(user_scroll),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_paned_add2(GTK_PANED(paned), user_scroll);
    gtk_paned_set_position(GTK_PANED(paned), 600);
    
        user_list_store = gtk_list_store_new(1, G_TYPE_STRING);
    user_list = gtk_tree_view_new_with_model(GTK_TREE_MODEL(user_list_store));
    gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(user_list), TRUE);
    
        GtkTreeViewColumn *column = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(column, "Online Users");
    
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(column, renderer, TRUE);
    gtk_tree_view_column_add_attribute(column, renderer, "text", 0);
    
    gtk_tree_view_append_column(GTK_TREE_VIEW(user_list), column);
    gtk_container_add(GTK_CONTAINER(user_scroll), user_list);
    
        GtkWidget *message_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start(GTK_BOX(main_box), message_box, FALSE, FALSE, 0);
    
        message_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(message_entry), "Type your message here");
    g_signal_connect(message_entry, "activate", G_CALLBACK(on_message_entry_activate), NULL);
    gtk_box_pack_start(GTK_BOX(message_box), message_entry, TRUE, TRUE, 0);
    gtk_widget_set_sensitive(message_entry, FALSE);
    
        send_button = gtk_button_new_with_label("Send");
    g_signal_connect(send_button, "clicked", G_CALLBACK(on_send_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(message_box), send_button, FALSE, FALSE, 0);
    gtk_widget_set_sensitive(send_button, FALSE);
    
        connect_dialog = gtk_dialog_new_with_buttons("Connect to Server",
                                                GTK_WINDOW(main_window),
                                                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                "Connect", GTK_RESPONSE_ACCEPT,
                                                "Cancel", GTK_RESPONSE_REJECT,
                                                NULL);
    
    GtkWidget *connect_content = gtk_dialog_get_content_area(GTK_DIALOG(connect_dialog));
    
        GtkWidget *connect_grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(connect_grid), 10);
    gtk_grid_set_column_spacing(GTK_GRID(connect_grid), 5);
    gtk_container_add(GTK_CONTAINER(connect_content), connect_grid);
    
        GtkWidget *server_label = gtk_label_new("Server IP:");
    gtk_grid_attach(GTK_GRID(connect_grid), server_label, 0, 0, 1, 1);
    
    server_ip_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(server_ip_entry), "127.0.0.1");
    gtk_grid_attach(GTK_GRID(connect_grid), server_ip_entry, 1, 0, 1, 1);
    
        GtkWidget *nickname_label = gtk_label_new("Nickname:");
    gtk_grid_attach(GTK_GRID(connect_grid), nickname_label, 0, 1, 1, 1);
    
    nickname_entry = gtk_entry_new();
    gtk_grid_attach(GTK_GRID(connect_grid), nickname_entry, 1, 1, 1, 1);
    
    g_signal_connect(connect_dialog, "response", G_CALLBACK(on_connect_dialog_response), NULL);

    nickname_dialog = connect_dialog;
    
        gtk_widget_show_all(main_window);
    
    return 0;
}

void gui_show_connect_dialog(void) {
    if (server_ip_entry) {
        gtk_entry_set_text(GTK_ENTRY(server_ip_entry), "127.0.0.1");
    }

    if (nickname_entry) {
        gtk_entry_set_text(GTK_ENTRY(nickname_entry), "");
    }

    gtk_widget_show_all(connect_dialog);
}

void gui_show_nickname_dialog(void) {
        if (nickname_entry) {
        gtk_entry_set_text(GTK_ENTRY(nickname_entry), "");
    }
    
        gui_show_connect_dialog();
}

void gui_show_main_window(void) {
    gtk_widget_show(main_window);
}

void gui_hide_main_window(void) {
    gtk_widget_hide(main_window);
}

void gui_add_chat_message(const char *username, const char *message) {
        if (!username || !message) {
        return;
    }
    
        if (!g_main_context_is_owner(g_main_context_default())) {
                        struct ChatMsgData {
            char username[MAX_USERNAME_LEN];
            char message[MAX_MESSAGE_LEN];
        } *data = g_malloc(sizeof(struct ChatMsgData));
        
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

            if (!chat_view) {
        return;
    }
    
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    if (!buffer) {
        return;
    }
    
        if (!gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "username")) {
        gtk_text_buffer_create_tag(buffer, "username", 
                                 "weight", PANGO_WEIGHT_BOLD, 
                                 "foreground", "blue", NULL);
    }
    
        GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    
        gtk_text_buffer_insert(buffer, &end, "\n", -1);
    
        gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &end, username, -1, "username", NULL);
    
        gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, ": ", -1);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, message, -1);
    
        gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chat_view), &end, 0.0, FALSE, 0.0, 0.0);
}

void gui_add_system_message(const char *message) {
        if (!message) {
        return;
    }
    
        if (!g_main_context_is_owner(g_main_context_default())) {
                char *msg_copy = g_strdup(message);
        g_idle_add((GSourceFunc)gui_add_system_message_idle, msg_copy);
        return;
    }

            if (!chat_view) {
        return;
    }
    
        GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    if (!buffer) {
        return;
    }
    
        if (!gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(buffer), "system")) {
        gtk_text_buffer_create_tag(buffer, "system", 
                                 "style", PANGO_STYLE_ITALIC, 
                                 "foreground", "gray", NULL);
    }
    
        GtkTextIter end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    
        gtk_text_buffer_insert(buffer, &end, "\n", -1);
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert_with_tags_by_name(buffer, &end, message, -1, "system", NULL);
    
        gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(chat_view), &end, 0.0, FALSE, 0.0, 0.0);
}

static gboolean gui_add_chat_message_idle(gpointer user_data) {
    struct ChatMsgData {
        char username[MAX_USERNAME_LEN];
        char message[MAX_MESSAGE_LEN];
    } *data = user_data;
    
        gui_add_chat_message(data->username, data->message);
    
        g_free(data);
    
        return FALSE;
}

static gboolean gui_add_system_message_idle(gpointer user_data) {
    char *message = (char *)user_data;
    
        gui_add_system_message(message);
    
        g_free(message);
    
        return FALSE;
}

void gui_update_user_list(const char *user_list, const int length) {
        if (!user_list || length <= 0) {
        logger_log(LOG_WARNING, "Invalid user list received (null or empty)");
        return;
    }
    
        if (user_list[length-1] != '\0') {
        logger_log(LOG_WARNING, "Invalid user list format: missing null terminator");
        return;
    }
    
        gtk_list_store_clear(user_list_store);
    
        const char *p = user_list;
    
        if (p && *p) {
        logger_log(LOG_DEBUG, "User list header: %s", p);
        
                const size_t header_len = strlen(p);
        if (header_len + 1 >= length) {
            logger_log(LOG_WARNING, "Invalid user list: header too long or missing data");
            return;
        }
        
                        p += header_len + 1;
        
                int user_count = 0;
        
        while (p < user_list + length && *p) {
                        const char* next = p;
            const size_t remaining = user_list + length - p;
            size_t name_len = 0;
            
                        while (name_len < remaining && next[name_len] != '\0') {
                name_len++;
            }
            
            if (name_len >= remaining) {
                logger_log(LOG_WARNING, "Invalid user list: missing null terminator for username");
                break;
            }
            
                        logger_log(LOG_DEBUG, "Adding user to list: %s", p);
            GtkTreeIter iter;
            gtk_list_store_append(user_list_store, &iter);
            gtk_list_store_set(user_list_store, &iter, 0, p, -1);
            
                        p += name_len + 1;
            user_count++;
        }
        
        logger_log(LOG_INFO, "Updated user list with %d users", user_count);
    } else {
        logger_log(LOG_WARNING, "Empty user list received");
    }
}

void gui_add_user(const char *username) {
        if (!g_main_context_is_owner(g_main_context_default())) {
                UserData *data = g_malloc(sizeof(UserData));
        strncpy(data->username, username, MAX_USERNAME_LEN - 1);
        data->username[MAX_USERNAME_LEN - 1] = '\0';
        g_idle_add((GSourceFunc)gui_add_user_idle, data);
        return;
    }

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
    
        gtk_list_store_append(user_list_store, &iter);
    gtk_list_store_set(user_list_store, &iter, 0, username, -1);
}

static gboolean gui_add_user_idle(gpointer user_data) {
    UserData *data = (UserData *)user_data;
    
        gui_add_user(data->username);
    
        g_free(data);
    
        return FALSE;
}

void gui_remove_user(const char *username) {
        if (!g_main_context_is_owner(g_main_context_default())) {
                UserData *data = g_malloc(sizeof(UserData));
        strncpy(data->username, username, MAX_USERNAME_LEN - 1);
        data->username[MAX_USERNAME_LEN - 1] = '\0';
        g_idle_add((GSourceFunc)gui_remove_user_idle, data);
        return;
    }

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

static gboolean gui_remove_user_idle(gpointer user_data) {
    UserData *data = (UserData *)user_data;
    
        gui_remove_user(data->username);
    
        g_free(data);
    
        return FALSE;
}

void gui_clear_chat(void) {
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(chat_view));
    gtk_text_buffer_set_text(buffer, "", -1);
}

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

void gui_main(void) {
    gtk_main();
}

void gui_cleanup(void) {
        if (!g_main_context_is_owner(g_main_context_default())) {
                logger_log(LOG_WARNING, "gui_cleanup called from non-main thread");
        return;     }

        while (gtk_events_pending()) {
        gtk_main_iteration();
    }

        if (user_list_store && G_IS_OBJECT(user_list_store)) {
        g_object_unref(user_list_store);
        user_list_store = NULL;
    }

        if (connect_dialog != NULL && GTK_IS_WIDGET(connect_dialog)) {
        gtk_widget_destroy(connect_dialog);
        connect_dialog = NULL;
    }
    
        if (nickname_dialog != NULL && nickname_dialog != connect_dialog && GTK_IS_WIDGET(nickname_dialog)) {
        gtk_widget_destroy(nickname_dialog);
        nickname_dialog = NULL;
    } else {
        nickname_dialog = NULL;
    }
    
        chat_view = NULL;
    message_entry = NULL;
    send_button = NULL;
    user_list = NULL;
    server_ip_entry = NULL;
    nickname_entry = NULL;

        if (main_window != NULL && GTK_IS_WIDGET(main_window)) {
        gtk_widget_destroy(main_window);
        main_window = NULL;
    }
}

static void on_connect_dialog_response(GtkDialog *dialog, gint response_id, gpointer user_data) {
    if (response_id == GTK_RESPONSE_ACCEPT) {
        const char *ip = gtk_entry_get_text(GTK_ENTRY(server_ip_entry));
        const char *nickname = gtk_entry_get_text(GTK_ENTRY(nickname_entry));
        
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
        
                gui_add_system_message("Connecting and setting nickname...");
        if (net_handler_connect_with_nickname(ip, nickname) != 0) {
            gui_add_system_message("Connection failed: Could not connect to server or set nickname.");
            gui_show_error("Connection Error", "Failed to connect to server or set nickname.");
            return;
        }
        
                gtk_widget_hide(GTK_WIDGET(dialog));
        
                gtk_widget_set_sensitive(message_entry, TRUE);
        gtk_widget_set_sensitive(send_button, TRUE);
    } else {
        gtk_widget_hide(GTK_WIDGET(dialog));
    }
}

static void on_send_clicked(GtkButton *button, gpointer user_data) {
        const char *message = gtk_entry_get_text(GTK_ENTRY(message_entry));
    
        if (!message || strlen(message) == 0) {
        return;
    }
    
        if (!net_handler_is_connected()) {
        gui_show_error("Not Connected", "You must connect to a server first.");
        return;
    }
    
    if (!net_handler_has_nickname()) {
        gui_show_error("No Nickname", "You must set a nickname before sending messages.");
        gui_show_nickname_dialog();
        return;
    }
    
        if (net_handler_send_message(message) != 0) {
        gui_show_error("Send Error", "Failed to send message.");
        return;
    }
    
        gtk_entry_set_text(GTK_ENTRY(message_entry), "");
}

static void on_message_entry_activate(GtkEntry *entry, gpointer user_data) {
    on_send_clicked(GTK_BUTTON(send_button), NULL);
}

static void on_disconnect_clicked(GtkButton *button, gpointer user_data) {
        net_handler_disconnect();
    
        gui_add_system_message("Disconnected from server.");
    
        gtk_list_store_clear(user_list_store);
    
        gtk_widget_set_sensitive(message_entry, FALSE);
    gtk_widget_set_sensitive(send_button, FALSE);
}

static void on_main_window_destroy(GtkWidget *widget, gpointer user_data) {
        net_handler_disconnect();
    
        fprintf(stderr, "Chat client shutting down...\n");
    
        gtk_main_quit();
}

static void on_nickname_response(NicknameResponse *response) {
    if (response->status == STATUS_SUCCESS) {
        gui_add_system_message("Nickname set successfully.");
        
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
        
                        g_idle_add((GSourceFunc)gui_show_connect_dialog, NULL);
    }
}

static void on_chat_message(const ChatMessage *message) {
    gui_add_chat_message(message->username, message->message);
}

static void on_user_join(UserNotification *notification) {
        char buffer[MAX_USERNAME_LEN + 32];
    snprintf(buffer, sizeof(buffer), "%s has joined the chat.", notification->username);
    
        gui_add_system_message(buffer);
    
        gui_add_user(notification->username);
}

static void on_user_leave(UserNotification *notification) {
    char buffer[MAX_USERNAME_LEN + 32];
    snprintf(buffer, sizeof(buffer), "%s has left the chat.", notification->username);
    gui_add_system_message(buffer);
    
        gui_remove_user(notification->username);
}

static void on_user_list(const char *user_list, const int length) {
        if (!user_list) {
        logger_log(LOG_WARNING, "Received NULL user list");
        return;
    }
    
    if (length <= 0 || length > MAX_MESSAGE_LEN) {
        logger_log(LOG_WARNING, "Received invalid user list length: %d", length);
        return;
    }
    
        logger_log(LOG_DEBUG, "Processing user list message of length %d", length);
    
    gui_update_user_list(user_list, length);
}

static void on_disconnect(void) {
    if (!g_main_context_is_owner(g_main_context_default())) {
                g_idle_add((GSourceFunc)on_disconnect, NULL);
        return;
    }

    gui_add_system_message("Disconnected from server.");
    
        if (user_list_store) {
        gtk_list_store_clear(user_list_store);
    }
    
        if (message_entry) {
        gtk_widget_set_sensitive(message_entry, FALSE);
    }
    
    if (send_button) {
        gtk_widget_set_sensitive(send_button, FALSE);
    }
}

static void on_connect_clicked(GtkButton *button, gpointer user_data) {
    if (net_handler_is_connected()) {
        gui_show_info("Already Connected", "You are already connected to a server.");
        return;
    }
    
        gui_show_connect_dialog();
}

int gui_is_fallback_mode(void) {
    return fallback_mode;
}

int gui_init_fallback(void) {
    fallback_mode = 1;
    return 0;
}

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

                size_t len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }

                if (strncmp(buffer, "/quit", 5) == 0) {
            break;
        }
        if (strncmp(buffer, "/connect ", 9) == 0) {
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

void gui_add_chat_message_fallback(const char *username, const char *message) {
    printf("%s: %s\n", username, message);
}

void gui_add_system_message_fallback(const char *message) {
    printf("[SYSTEM] %s\n", message);
}

