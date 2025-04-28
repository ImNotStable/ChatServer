/**
 * @file chat_handler.c
 * @brief Implementation of the chat handler module
 *
 * This file implements client connection handling, message routing,
 * user tracking, and broadcasting messages to connected clients.
 *
 * @author Jeremiah Hughes & Anthony Patton
 * @date April 2025
 */

#include "chat_handler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>
#include "../common/logger.h"
#include "../common/protocol.h"
#include <errno.h>
#include <netinet/in.h>

/** Maximum number of concurrent clients */
#define MAX_CLIENTS 100

/** Array of active clients */
Client *clients[MAX_CLIENTS] = {NULL};

/** Number of active clients */
static int client_count = 0;

/** Next client ID to assign */
static int next_client_id = 1;

/** Mutex for thread-safe access to the clients array */
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

/** Array to track client threads for cleanup */
static pthread_t client_threads[MAX_CLIENTS] = {0};
static int thread_count = 0;
static pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_t current_joining_thread = 0;
static volatile sig_atomic_t alarm_fired = 0;

static void alarm_handler(int sig) {
    alarm_fired = 1;
    if (current_joining_thread != 0) {
        pthread_cancel(current_joining_thread);
    }
}

/**
 * @brief Initializes the chat handler module
 *
 * This function initializes the client tracking structures.
 *
 * @return 0 on success, -1 on failure
 */
int chat_handler_init(void) {
    pthread_mutex_lock(&clients_mutex);
    memset(clients, 0, sizeof(clients));
    client_count = 0;
    next_client_id = 1;
    pthread_mutex_unlock(&clients_mutex);

    pthread_mutex_lock(&thread_mutex);
    memset(client_threads, 0, sizeof(client_threads));
    thread_count = 0;
    pthread_mutex_unlock(&thread_mutex);

    logger_log(LOG_DEBUG, "Structure sizes - NicknameRequest: %zu, ChatMessage: %zu, UserNotification: %zu",
               sizeof(NicknameRequest), sizeof(ChatMessage), sizeof(UserNotification));

    return 0;
}

/**
 * @brief Cleans up the chat handler module
 *
 * This function frees resources used by the chat handler module,
 * including closing client sockets and freeing client structures.
 */
void chat_handler_cleanup(void) {
    pthread_mutex_lock(&thread_mutex);

    struct sigaction sa;
    struct sigaction old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = alarm_handler;
    sigaction(SIGALRM, &sa, &old_sa);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_threads[i] != 0) {
            alarm_fired = 0;
            current_joining_thread = client_threads[i];

            alarm(2);

            int join_result = pthread_join(current_joining_thread, NULL);

            alarm(0);

            if (join_result != 0 || alarm_fired) {
                logger_log(LOG_WARNING, "Thread %lu was %s during cleanup",
                           (unsigned long) current_joining_thread,
                           alarm_fired ? "timed out and cancelled" : "not joinable");

                if (alarm_fired) {
                    usleep(10000);
                    pthread_join(current_joining_thread, NULL);
                }
            }

            current_joining_thread = 0;
            client_threads[i] = 0;
        }
    }
    thread_count = 0;
    pthread_mutex_unlock(&thread_mutex);

    sigaction(SIGALRM, &old_sa, NULL);

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            close(clients[i]->socket);
            free(clients[i]);
            clients[i] = NULL;
        }
    }

    client_count = 0;

    pthread_mutex_unlock(&clients_mutex);

    pthread_mutex_destroy(&clients_mutex);
    pthread_mutex_destroy(&thread_mutex);
}

/**
 * @brief Adds a client to the active client list
 *
 * This function creates a new client structure for the connected client
 * and adds it to the active client list.
 *
 * @param client_socket Socket for the connected client
 * @return The client ID on success, -1 on failure
 */
int chat_handler_add_client(const int client_socket) {
    pthread_mutex_lock(&clients_mutex);

    if (client_count >= MAX_CLIENTS) {
        pthread_mutex_unlock(&clients_mutex);
        logger_log(LOG_WARNING, "Maximum number of clients reached");
        return -1;
    }

    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!clients[i]) {
            slot = i;
            break;
        }
    }

    if (slot == -1) {
        pthread_mutex_unlock(&clients_mutex);
        logger_log(LOG_ERROR, "Failed to find an empty slot for client");
        return -1;
    }

    Client *client = (Client *) malloc(sizeof(Client));
    if (!client) {
        pthread_mutex_unlock(&clients_mutex);
        logger_log(LOG_ERROR, "Failed to allocate memory for client");
        return -1;
    }

    client->socket = client_socket;
    client->id = next_client_id++;
    client->has_nickname = 0;
    memset(client->nickname, 0, sizeof(client->nickname));

    const int client_id = client->id;

    if (pthread_create(&client->thread, NULL, chat_handler_client_thread, client) != 0) {
        free(client);
        pthread_mutex_unlock(&clients_mutex);
        logger_log(LOG_ERROR, "Failed to create client thread");
        return -1;
    }

    clients[slot] = client;
    client_count++;

    pthread_mutex_lock(&thread_mutex);
    int thread_slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (client_threads[i] == 0) {
            thread_slot = i;
            client_threads[i] = client->thread;
            thread_count++;
            break;
        }
    }
    pthread_mutex_unlock(&thread_mutex);

    if (thread_slot == -1) {
        logger_log(LOG_WARNING, "Failed to track client thread - thread array full");
        clients[slot] = NULL;
        client_count--;

        pthread_cancel(client->thread);
        pthread_join(client->thread, NULL);

        free(client);

        pthread_mutex_unlock(&clients_mutex);
        logger_log(LOG_ERROR, "Client cleanup due to thread tracking failure");
        return -1;
    }


    pthread_mutex_unlock(&clients_mutex);

    logger_log(LOG_INFO, "Added client %d to slot %d", client_id, slot);
    return client_id;
}

/**
 * @brief Finds a client by ID
 *
 * This function finds a client by ID in the clients array.
 * The clients_mutex must be locked before calling this function.
 *
 * @param client_id ID of the client to find
 * @return Index of the client in the clients array, or -1 if not found
 */
static int find_client_slot(const int client_id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->id == client_id) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief Safely copies a nickname with proper null termination
 *
 * @param dest Destination buffer
 * @param src Source string
 * @param size Size of the destination buffer
 */
static void safe_nickname_copy(char *dest, const char *src, const size_t size) {
    strncpy(dest, src, size - 1);
    dest[size - 1] = '\0';
}

/**
 * @brief Removes a client from the active client list
 *
 * This function removes a client from the active client list and
 * frees its resources. If the client has a nickname, it also notifies
 * other clients that the user has left.
 *
 * @param client_id ID of the client to remove
 */
void chat_handler_remove_client(const int client_id) {
    pthread_mutex_lock(&clients_mutex);

    int found = 0;
    char nickname[MAX_USERNAME_LEN] = {0};
    int user_had_nickname = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->id == client_id) {
            Client *client = clients[i];

            if (client->has_nickname) {
                user_had_nickname = 1;
                strncpy(nickname, client->nickname, MAX_USERNAME_LEN);
                nickname[MAX_USERNAME_LEN - 1] = '\0';
            }

            logger_log(LOG_INFO, "Removing client %d: %s", client->id, client->nickname);

            clients[i] = NULL;
            client_count--;

            pthread_mutex_unlock(&clients_mutex);

            if (client->socket >= 0) {
                int result = close(client->socket);
                if (result != 0) {
                    logger_log(LOG_WARNING, "Failed to close socket for client %d: %s",
                               client->id, strerror(errno));
                }
                client->socket = -1;
            }

            int detach_result = pthread_detach(client->thread);
            if (detach_result != 0) {
                logger_log(LOG_WARNING, "Failed to detach thread for client %d: %s",
                           client->id, strerror(detach_result));
            }

            free(client);

            found = 1;
            break;
        }
    }

    if (!found) {
        pthread_mutex_unlock(&clients_mutex);
        logger_log(LOG_WARNING, "Failed to remove client %d: not found", client_id);
        return;
    }

    logger_log(LOG_INFO, "Removed client %d", client_id);

    if (user_had_nickname) {
        chat_handler_user_left(nickname);
        return;
    }

    int client_sockets[MAX_CLIENTS];
    int socket_count = 0;

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->has_nickname) {
            client_sockets[socket_count++] = clients[i]->socket;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    if (socket_count > 0) {
        for (int i = 0; i < socket_count; i++) {
            send_user_list(client_sockets[i]);
        }
        logger_log(LOG_INFO, "Broadcast updated user list after client %d disconnected", client_id);
    }
}

/**
 * @brief Thread function for handling a client connection
 *
 * This function runs in a separate thread for each connected client
 * and handles messages from the client.
 *
 * @param arg Pointer to the Client structure
 * @return NULL
 */
void *chat_handler_client_thread(void *arg) {
    Client *client = arg;
    const int client_id = client->id;
    const int socket_fd = client->socket;

    logger_log(LOG_INFO, "Client thread started for client %d (socket %d)", client_id, socket_fd);

    pthread_cleanup_push((void (*)(void *))chat_handler_remove_client, (void *)(intptr_t)client_id)
        ;

        while (1) {
            errno = 0;
            logger_log(LOG_DEBUG, "Client Thread %d: Waiting to receive message...", client_id);

            MessageHeader header;
            const int header_res = recv(socket_fd, &header, sizeof(MessageHeader), 0);

            if (header_res <= 0) {
                if (header_res == 0) {
                    logger_log(LOG_INFO, "Client %d disconnected (header receive returned 0)", client_id);
                } else {
                    logger_log(LOG_WARNING, "Client %d header receive error (returned %d). Errno: %d (%s)",
                               client_id, header_res, errno, strerror(errno));
                }
                break;
            }

            const MessageType type = header.type;
            uint32_t length = ntohl(header.length);

            logger_log(LOG_DEBUG, "Client Thread %d: Received header. Type=%d, Length=%u", client_id, type, length);

            size_t expected_size = 0;
            size_t max_size = MAX_MESSAGE_LEN;
            switch (type) {
                case MSG_NICKNAME:
                    expected_size = sizeof(NicknameRequest);
                    max_size = sizeof(NicknameRequest) + 32;
                    break;
                case MSG_CHAT:
                    expected_size = sizeof(ChatMessage);
                    max_size = MAX_USERNAME_LEN + MAX_MESSAGE_LEN + 64;
                    break;
                case MSG_DISCONNECT:
                    expected_size = 0;
                    max_size = 8;
                    break;
                default:
                    expected_size = 0;
                    max_size = MAX_MESSAGE_LEN;
            }

            logger_log(LOG_DEBUG, "Message validation: type=%d, length=%u, expected_size=%zu, max_size=%zu",
                       type, length, expected_size, max_size);

            if ((expected_size > 0 && length < expected_size) || length > max_size) {
                if (expected_size > 0 && length < expected_size) {
                    logger_log(LOG_WARNING,
                               "Client %d sent a message with insufficient size (%u bytes). Minimum expected size for message type %d is %zu bytes.",
                               client_id, length, type, expected_size);
                } else {
                    logger_log(LOG_WARNING,
                               "Client %d sent a message that's too large (%u bytes). Maximum allowed for type %d is %zu bytes.",
                               client_id, length, type, max_size);
                }

                char discard_buffer[1024];
                size_t remaining = length;
                while (remaining > 0) {
                    size_t to_read = remaining < sizeof(discard_buffer) ? remaining : sizeof(discard_buffer);
                    ssize_t read_bytes = recv(socket_fd, discard_buffer, to_read, 0);
                    if (read_bytes <= 0) {
                        break;
                    }
                    remaining -= read_bytes;
                }

                NicknameResponse resp = {0};
                resp.status = STATUS_ERROR;
                strcpy(resp.message, expected_size > 0 && length < expected_size
                                         ? "Message too small"
                                         : "Message too large");
                send_message(socket_fd, MSG_NICKNAME_RESPONSE, &resp, sizeof(resp));

                continue;
            }

            uint8_t data_buffer[MAX_MESSAGE_LEN] = {0};
            if (length > 0) {
                const int data_res = recv(socket_fd, data_buffer, length, 0);
                if (data_res <= 0) {
                    if (data_res == 0) {
                        logger_log(LOG_INFO, "Client %d disconnected (data receive returned 0)", client_id);
                    } else {
                        logger_log(LOG_WARNING, "Client %d data receive error (returned %d). Errno: %d (%s)",
                                   client_id, data_res, errno, strerror(errno));
                    }
                    break;
                }

                if (data_res != length) {
                    logger_log(LOG_WARNING, "Client %d: Partial data received (%d of %u bytes)",
                               client_id, data_res, length);
                    continue;
                }
            }

            logger_log(LOG_DEBUG, "Client Thread %d: Received complete message. Type=%d, Length=%u", client_id, type,
                       length);

            switch (type) {
                case MSG_NICKNAME: {
                    NicknameRequest *req = (NicknameRequest *) data_buffer;
                    NicknameResponse resp = {0};

                    req->nickname[MAX_USERNAME_LEN - 1] = '\0';

                    logger_log(LOG_INFO, "Nickname request from client %d, nickname: '%s', length: %zu, data size: %u",
                               client_id, req->nickname, strlen(req->nickname), length);

                    if (strlen(req->nickname) < 2) {
                        logger_log(LOG_WARNING, "Nickname too short - first bytes: [%02X %02X %02X %02X]",
                                   (unsigned char) req->nickname[0],
                                   (unsigned char) req->nickname[1],
                                   (unsigned char) req->nickname[2],
                                   (unsigned char) req->nickname[3]);

                        resp.status = STATUS_ERROR;
                        strcpy(resp.message, "Nickname too short (minimum 2 characters)");
                        logger_log(LOG_WARNING, "Connection rejected: %s", resp.message);

                        send_message(socket_fd, MSG_NICKNAME_RESPONSE, &resp, sizeof(resp));

                        break;
                    }

                    if (chat_handler_is_nickname_taken(req->nickname)) {
                        resp.status = STATUS_NICKNAME_TAKEN;
                        strcpy(resp.message, "Nickname is already in use");
                        logger_log(LOG_WARNING, "Connection rejected: %s already in use", req->nickname);

                        send_message(socket_fd, MSG_NICKNAME_RESPONSE, &resp, sizeof(resp));

                        break;
                    }

                    pthread_mutex_lock(&clients_mutex);
                    int slot = find_client_slot(client_id);
                    if (slot != -1) {
                        safe_nickname_copy(clients[slot]->nickname, req->nickname, sizeof(clients[slot]->nickname));
                        clients[slot]->has_nickname = 1;
                    }
                    pthread_mutex_unlock(&clients_mutex);

                    resp.status = STATUS_SUCCESS;
                    strcpy(resp.message, "Nickname set successfully");
                    send_message(socket_fd, MSG_NICKNAME_RESPONSE, &resp, sizeof(resp));

                    char welcome_msg[MAX_MESSAGE_LEN];
                    snprintf(welcome_msg, sizeof(welcome_msg),
                             "Welcome to the chat server, %s! You are now fully connected.", req->nickname);
                    chat_handler_send_message(client_id, welcome_msg);

                    int user_count = 0;
                    pthread_mutex_lock(&clients_mutex);
                    for (int i = 0; i < MAX_CLIENTS; i++) {
                        if (clients[i] && clients[i]->has_nickname && clients[i]->id != client_id) {
                            user_count++;
                        }
                    }
                    pthread_mutex_unlock(&clients_mutex);

                    if (user_count > 0) {
                        char users_msg[MAX_MESSAGE_LEN];
                        snprintf(users_msg, sizeof(users_msg), "There %s %d other user%s in the chat.",
                                 (user_count == 1) ? "is" : "are", user_count, (user_count == 1) ? "" : "s");
                        chat_handler_send_message(client_id, users_msg);
                    }

                    chat_handler_user_joined(req->nickname);

                    send_user_list(socket_fd);

                    logger_log(LOG_INFO, "Client %d nickname set to %s", client_id, req->nickname);
                    continue;
                }

                case MSG_CHAT: {
                    ChatMessage *msg = (ChatMessage *) data_buffer;
                    char nickname[MAX_USERNAME_LEN];

                    pthread_mutex_lock(&clients_mutex);
                    const int has_nickname = client->has_nickname;
                    if (has_nickname) {
                        safe_nickname_copy(nickname, client->nickname, sizeof(nickname));
                    }
                    pthread_mutex_unlock(&clients_mutex);

                    if (!has_nickname) {
                        logger_log(LOG_WARNING, "Client %d tried to send a message without setting a nickname",
                                   client_id);
                        chat_handler_send_message(client_id, "You must set a nickname before sending messages");
                        continue;
                    }

                    msg->message[MAX_MESSAGE_LEN - 1] = '\0';

                    logger_log(LOG_INFO, "Chat message from %s: %s",
                               nickname, msg->message);

                    chat_handler_broadcast_message(nickname, msg->message);
                    break;
                }

                case MSG_DISCONNECT: {
                    logger_log(LOG_INFO, "Client %d requested disconnection", client_id);
                    pthread_exit(NULL);
                }

                default: {
                    logger_log(LOG_WARNING, "Received unsupported message type %d from client %d",
                               type, client_id);
                    break;
                }
            }
        }

    pthread_cleanup_pop(1);
    return NULL;
}

/**
 * @brief Checks if a nickname is already in use
 *
 * This function checks if a nickname is already in use by another client.
 *
 * @param nickname The nickname to check
 * @return 1 if the nickname is already in use, 0 otherwise
 */
int chat_handler_is_nickname_taken(const char *nickname) {
    int taken = 0;

    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->has_nickname &&
            strcmp(clients[i]->nickname, nickname) == 0) {
            taken = 1;
            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    return taken;
}

/**
 * @brief Broadcasts a message to all clients with nicknames
 *
 * This function sends a message to all clients with set nicknames.
 *
 * @param sender Nickname of the message sender
 * @param message The message text
 */
void chat_handler_broadcast_message(const char *sender, const char *message) {
    ChatMessage msg;
    safe_nickname_copy(msg.username, sender, sizeof(msg.username));
    strncpy(msg.message, message, sizeof(msg.message) - 1);
    msg.message[sizeof(msg.message) - 1] = '\0';

    pthread_mutex_lock(&clients_mutex);

    int client_sockets[MAX_CLIENTS];
    int socket_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->has_nickname) {
            client_sockets[socket_count++] = clients[i]->socket;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    for (int i = 0; i < socket_count; i++) {
        send_message(client_sockets[i], MSG_CHAT, &msg, sizeof(msg));
    }
}

/**
 * @brief Broadcasts a user join notification
 *
 * This function notifies all clients with nicknames that a user has joined.
 *
 * @param nickname Nickname of the user who joined
 */
void chat_handler_user_joined(const char *nickname) {
    UserNotification notify;
    safe_nickname_copy(notify.username, nickname, sizeof(notify.username));

    pthread_mutex_lock(&clients_mutex);

    int client_sockets[MAX_CLIENTS];
    int socket_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->has_nickname &&
            strcmp(clients[i]->nickname, nickname) != 0) {
            client_sockets[socket_count++] = clients[i]->socket;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    for (int i = 0; i < socket_count; i++) {
        send_message(client_sockets[i], MSG_USER_JOIN, &notify, sizeof(notify));
    }

    logger_log(LOG_INFO, "Broadcast user joined: %s", nickname);

    pthread_mutex_lock(&clients_mutex);

    memset(client_sockets, 0, sizeof(client_sockets));
    socket_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->has_nickname) {
            client_sockets[socket_count++] = clients[i]->socket;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    for (int i = 0; i < socket_count; i++) {
        send_user_list(client_sockets[i]);
    }

    logger_log(LOG_INFO, "Broadcast updated user list after user joined: %s", nickname);
}

/**
 * @brief Broadcasts a user leave notification
 *
 * This function notifies all clients with nicknames that a user has left.
 *
 * @param nickname Nickname of the user who left
 */
void chat_handler_user_left(const char *nickname) {
    UserNotification notify;
    safe_nickname_copy(notify.username, nickname, sizeof(notify.username));

    pthread_mutex_lock(&clients_mutex);

    int client_sockets[MAX_CLIENTS];
    int socket_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->has_nickname &&
            strcmp(clients[i]->nickname, nickname) != 0) {
            client_sockets[socket_count++] = clients[i]->socket;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    for (int i = 0; i < socket_count; i++) {
        send_message(client_sockets[i], MSG_USER_LEAVE, &notify, sizeof(notify));
    }

    logger_log(LOG_INFO, "Broadcast user left: %s", nickname);

    if (socket_count > 0) {
        for (int i = 0; i < socket_count; i++) {
            send_user_list(client_sockets[i]);
        }

        logger_log(LOG_INFO, "Broadcast updated user list after user left: %s", nickname);
    }
}

/**
 * @brief Sets the nickname for a client
 *
 * This function sets the nickname for a client identified by client_id.
 *
 * @param client_id ID of the client
 * @param nickname Nickname to set
 * @return 0 on success, 1 if nickname is already taken, -1 if client_id invalid
 */
int chat_handler_set_nickname(const int client_id, const char *nickname) {
    if (chat_handler_is_nickname_taken(nickname)) {
        return 1;
    }

    pthread_mutex_lock(&clients_mutex);

    const int slot = find_client_slot(client_id);

    if (slot == -1) {
        pthread_mutex_unlock(&clients_mutex);
        return -1;
    }

    safe_nickname_copy(clients[slot]->nickname, nickname, sizeof(clients[slot]->nickname));
    clients[slot]->has_nickname = 1;

    pthread_mutex_unlock(&clients_mutex);

    return 0;
}

/**
 * @brief Gets the nickname for a client
 *
 * This function retrieves the nickname for a client identified by client_id.
 *
 * @param client_id ID of the client
 * @param nickname_buf Buffer to store the nickname
 * @return 0 on success, -1 if client not found or has no nickname
 */
int chat_handler_get_nickname(const int client_id, char *nickname_buf) {
    pthread_mutex_lock(&clients_mutex);

    const int slot = find_client_slot(client_id);

    if (slot == -1 || !clients[slot]->has_nickname) {
        pthread_mutex_unlock(&clients_mutex);
        return -1;
    }

    safe_nickname_copy(nickname_buf, clients[slot]->nickname, MAX_USERNAME_LEN);

    pthread_mutex_unlock(&clients_mutex);

    return 0;
}

/**
 * @brief Sends a message to a specific client
 *
 * This function sends a text message to a specific client.
 *
 * @param client_id ID of the client to send the message to
 * @param message The message text
 * @return 0 on success, -1 on failure
 */
int chat_handler_send_message(const int client_id, const char *message) {
    int result = -1;
    int client_socket = -1;

    pthread_mutex_lock(&clients_mutex);

    const int slot = find_client_slot(client_id);

    if (slot != -1) {
        client_socket = clients[slot]->socket;
    }

    pthread_mutex_unlock(&clients_mutex);

    if (client_socket != -1) {
        ChatMessage msg;
        safe_nickname_copy(msg.username, "Server", sizeof(msg.username));
        strncpy(msg.message, message, sizeof(msg.message) - 1);
        msg.message[sizeof(msg.message) - 1] = '\0';

        if (send_message(client_socket, MSG_CHAT, &msg, sizeof(msg)) > 0) {
            result = 0;
        } else {
            logger_log(LOG_WARNING, "Failed to send message to client %d", client_id);
        }
    } else {
        logger_log(LOG_WARNING, "Failed to send message: client %d not found", client_id);
    }

    return result;
}

/**
 * @brief Builds a list of users with nicknames
 *
 * This function builds a list of all users with nicknames and stores it in the provided buffer.
 * The format is "Users" followed by each username on a separate line (null-terminated).
 *
 * @param buffer Buffer to store the user list
 * @param buffer_size Size of the buffer
 */
void chat_handler_get_online_users(char *buffer, const size_t buffer_size) {
    pthread_mutex_lock(&clients_mutex);

    memset(buffer, 0, buffer_size);

    strncpy(buffer, "Users", buffer_size - 1);
    buffer[buffer_size - 1] = '\0';

    size_t offset = strlen(buffer) + 1;
    int count = 0;

    for (int i = 0; i < MAX_CLIENTS && offset < buffer_size - 1; i++) {
        if (clients[i] && clients[i]->has_nickname) {
            const size_t nickname_len = strlen(clients[i]->nickname);
            if (offset + nickname_len + 1 < buffer_size) {
                strncpy(buffer + offset, clients[i]->nickname, buffer_size - offset - 1);
                buffer[offset + nickname_len] = '\0';
                offset += nickname_len + 1;
                count++;
            } else {
                break;
            }
        }
    }

    if (count == 0 && offset + 9 < buffer_size) {
        strncpy(buffer + offset, "No users", buffer_size - offset - 1);
        buffer[offset + 8] = '\0';
    }

    pthread_mutex_unlock(&clients_mutex);
}

/**
 * @brief Broadcasts a message to all connected clients
 *
 * This function sends a message to all connected clients, except
 * the one specified by exclude_socket (if not -1).
 *
 * @param type The message type
 * @param data The message data
 * @param data_length The length of the message data
 * @param exclude_socket Socket to exclude from broadcast, or -1 to broadcast to all
 */
void broadcast_message(const MessageType type, const void *data, const uint32_t data_length, const int exclude_socket) {
    pthread_mutex_lock(&clients_mutex);

    int client_sockets[MAX_CLIENTS];
    int socket_count = 0;

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && clients[i]->socket != exclude_socket) {
            client_sockets[socket_count++] = clients[i]->socket;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    for (int i = 0; i < socket_count; i++) {
        send_message(client_sockets[i], type, data, data_length);
    }
}

/**
 * @brief Sends the list of active users to a client
 *
 * This function creates a list of all users with nicknames and sends it
 * to the specified client.
 *
 * @param client_socket Socket of the client to send the list to
 */
void send_user_list(const int client_socket) {
    char buffer[MAX_MESSAGE_LEN] = {0};

    chat_handler_get_online_users(buffer, sizeof(buffer));

    size_t total_size = 0;
    int count = 0;
    size_t pos = 0;

    while (pos < sizeof(buffer) && count < MAX_CLIENTS + 2) {
        size_t len = strlen(buffer + pos);
        if (len == 0) {
            break;
        }

        pos += len + 1;
        count++;
        total_size = pos;
    }

    if (total_size == 0) {
        total_size = strlen(buffer) + 1;
    }

    send_message(client_socket, MSG_USER_LIST, buffer, total_size);
}
