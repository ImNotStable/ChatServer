#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "net_handler.h"
#include "../common/logger.h"
#include "../common/protocol.h"

static int socket_fd = -1;
static int connected = 0;
static int has_nickname = 0;
static char nickname[MAX_USERNAME_LEN];
static pthread_t receive_thread;
static int receiving = 0;

static NicknameResponseCallback nickname_callback = NULL;
static ChatMessageCallback chat_callback = NULL;
static UserJoinCallback user_join_callback = NULL;
static UserLeaveCallback user_leave_callback = NULL;
static UserListCallback user_list_callback = NULL;
static DisconnectCallback disconnect_callback = NULL;

static pthread_mutex_t net_mutex = PTHREAD_MUTEX_INITIALIZER;

static void __attribute__((destructor)) net_handler_cleanup(void) {
        net_handler_disconnect();
    
        pthread_mutex_destroy(&net_mutex);
    
    logger_log(LOG_DEBUG, "Network handler resources cleaned up");
}

int net_handler_init(void) {
    socket_fd = -1;
    connected = 0;
    has_nickname = 0;
    memset(nickname, 0, sizeof(nickname));
    receiving = 0;
    
    return 0;
}

static void log_connection_error(const char *message) {
    logger_log(LOG_ERROR, "%s", message);
    
        if (chat_callback) {
        ChatMessage msg;
        strcpy(msg.username, "System");
        strncpy(msg.message, message, MAX_MESSAGE_LEN - 1);
        msg.message[MAX_MESSAGE_LEN - 1] = '\0';
        chat_callback(&msg);
    }
}

int net_handler_connect(const char *server_ip) {
    pthread_mutex_lock(&net_mutex);
    
    if (connected) {
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_WARNING, "Already connected to server");
        return 0;
    }
    
        socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0) {
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_ERROR, "Failed to create socket");
        log_connection_error("Failed to create socket");
        return -1;
    }
    
        struct timeval timeout;
    timeout.tv_sec = 0;      timeout.tv_usec = 500000;      
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        logger_log(LOG_WARNING, "Failed to set socket receive timeout: %s", strerror(errno));
            }
    
        struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        close(socket_fd);
        socket_fd = -1;
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_ERROR, "Invalid server IP address");
        log_connection_error("Invalid server IP address");
        return -1;
    }
    
        if (connect(socket_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd);
        socket_fd = -1;
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_ERROR, "Failed to connect to server");
        log_connection_error("Failed to connect to server");
        return -1;
    }
    
        connected = 1;
    
    pthread_mutex_unlock(&net_mutex);
    
    logger_log(LOG_INFO, "Connected to server at %s:%d", server_ip, SERVER_PORT);
    
        return net_handler_start_receiving();
}

void net_handler_disconnect(void) {
        net_handler_stop_receiving();
    
    pthread_mutex_lock(&net_mutex);
    
    if (connected && socket_fd != -1) {
                        if (connected) {
                        uint8_t buffer[sizeof(MessageHeader)];
            memset(buffer, 0, sizeof(buffer));
            MessageHeader *header = (MessageHeader *)buffer;
            header->type = MSG_DISCONNECT;
            header->length = 0;
            
                        send(socket_fd, buffer, sizeof(buffer), 0);
        }
        
                shutdown(socket_fd, SHUT_RDWR);
        close(socket_fd);
        socket_fd = -1;
        connected = 0;
        has_nickname = 0;
    }
    
    pthread_mutex_unlock(&net_mutex);
    
    logger_log(LOG_INFO, "Disconnected from server");
}

static void *receive_thread_func(void *arg) {
    while (receiving) {
        MessageType type;
        uint8_t buffer[8192];         uint32_t length;
        
        pthread_mutex_lock(&net_mutex);
        const int is_connected = connected;
        const int sock = socket_fd;
        pthread_mutex_unlock(&net_mutex);
        
        if (!is_connected || sock == -1) {
            log_connection_error("Connection lost: Socket closed or not connected");
            break;
        }
        
        const int result = receive_message(sock, &type, buffer, &length);
        
        if (result == 0) {
                        pthread_mutex_lock(&net_mutex);
            connected = 0;
            has_nickname = 0;
            close(socket_fd);
            socket_fd = -1;
            pthread_mutex_unlock(&net_mutex);
            
            log_connection_error("Connection closed by server");
            
                        if (disconnect_callback) {
                disconnect_callback();
            }
            
            break;
        } else if (result < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                if (!receiving) {
                                        break;
                }
                
                                continue;
            }
            
                        pthread_mutex_lock(&net_mutex);
            connected = 0;
            has_nickname = 0;
            close(socket_fd);
            socket_fd = -1;
            pthread_mutex_unlock(&net_mutex);
            
            log_connection_error("Connection error: Failed to receive data from server");
            
                        if (disconnect_callback) {
                disconnect_callback();
            }
            
            break;
        }
        
                switch (type) {
            case MSG_NICKNAME_RESPONSE: {
                NicknameResponse *resp = (NicknameResponse *)buffer;
                logger_log(LOG_INFO, "Received nickname response: %s", resp->message);
                
                if (resp->status == STATUS_SUCCESS) {
                    pthread_mutex_lock(&net_mutex);
                    has_nickname = 1;
                    pthread_mutex_unlock(&net_mutex);
                } else {
                                        logger_log(LOG_WARNING, "Nickname rejected by server: %s", resp->message);
                    
                                        char error_msg[MAX_MESSAGE_LEN];
                                                            snprintf(error_msg, sizeof(error_msg), "Connection rejected: %.980s", 
                             resp->message);                     log_connection_error(error_msg);
                    
                                        if (nickname_callback) {
                        nickname_callback(resp);
                    }
                    
                                        pthread_mutex_lock(&net_mutex);
                    connected = 0;
                    has_nickname = 0;
                    close(socket_fd);
                    socket_fd = -1;
                    pthread_mutex_unlock(&net_mutex);
                    
                                        if (disconnect_callback) {
                        disconnect_callback();
                    }
                    
                    receiving = 0;
                    return NULL;                 }
                
                if (nickname_callback) {
                    nickname_callback(resp);
                }
                break;
            }
            
            case MSG_CHAT: {
                ChatMessage *msg = (ChatMessage *)buffer;
                logger_log(LOG_INFO, "Received chat message from %s: %s", msg->username, msg->message);
                
                if (chat_callback) {
                    chat_callback(msg);
                }
                break;
            }
            
            case MSG_USER_JOIN: {
                UserNotification *notify = (UserNotification *)buffer;
                logger_log(LOG_INFO, "User joined: %s", notify->username);
                
                if (user_join_callback) {
                    user_join_callback(notify);
                }
                break;
            }
            
            case MSG_USER_LEAVE: {
                UserNotification *notify = (UserNotification *)buffer;
                logger_log(LOG_INFO, "User left: %s", notify->username);
                
                if (user_leave_callback) {
                    user_leave_callback(notify);
                }
                break;
            }
              case MSG_USER_LIST: {
                logger_log(LOG_INFO, "Received user list of length %u", length);
                
                // Validate user list data
                if (length < 6) { // At minimum, "Users" + null terminator
                    logger_log(LOG_WARNING, "Received invalid user list (too short: %u bytes)", length);
                    break;
                }
                
                // Verify buffer is null-terminated
                if (buffer[length-1] != '\0') {
                    logger_log(LOG_WARNING, "Received improperly terminated user list");
                    // Add a null terminator to be safe
                    if (length < sizeof(buffer)) {
                        buffer[length] = '\0';
                    } else {
                        buffer[sizeof(buffer) - 1] = '\0';
                        logger_log(LOG_ERROR, "User list buffer size exceeded");
                        break;
                    }
                }
                
                // Verify first string is "Users" header
                if (strcmp((const char *)buffer, "Users") != 0) {
                    logger_log(LOG_WARNING, "Invalid user list format: missing 'Users' header");
                }
                
                if (user_list_callback) {
                    user_list_callback((const char *)buffer, length);
                }
                break;
            }
            
            case MSG_DISCONNECT: {
                logger_log(LOG_INFO, "Received disconnect message from server");
                
                pthread_mutex_lock(&net_mutex);
                connected = 0;
                has_nickname = 0;
                close(socket_fd);
                socket_fd = -1;
                pthread_mutex_unlock(&net_mutex);
                
                if (disconnect_callback) {
                    disconnect_callback();
                }
                
                receiving = 0;
                break;
            }
            
            default: {
                logger_log(LOG_WARNING, "Received unknown message type: %d", type);
                break;
            }
        }
    }
    
    logger_log(LOG_INFO, "Receive thread stopped");
    
    return NULL;
}

int net_handler_start_receiving(void) {
    pthread_mutex_lock(&net_mutex);
    
    if (receiving) {
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_WARNING, "Receive thread already running");
        return 0;
    }
    
    if (!connected) {
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_WARNING, "Not connected to server");
        return -1;
    }
    
    receiving = 1;
    
        pthread_attr_t attr;
    pthread_attr_init(&attr);
    
            pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    
    if (pthread_create(&receive_thread, &attr, receive_thread_func, NULL) != 0) {
        receiving = 0;
        pthread_attr_destroy(&attr);
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_ERROR, "Failed to create receive thread");
        return -1;
    }
    
        pthread_attr_destroy(&attr);
    
    pthread_mutex_unlock(&net_mutex);
    
    logger_log(LOG_INFO, "Started receive thread");
    
    return 0;
}

void net_handler_stop_receiving(void) {
    pthread_mutex_lock(&net_mutex);
    
    if (!receiving) {
        pthread_mutex_unlock(&net_mutex);
        return;
    }
    
    receiving = 0;
    
        if (connected && socket_fd != -1) {
                shutdown(socket_fd, SHUT_RD);
    }
    
        pthread_t thread_to_join = receive_thread;
        receive_thread = 0;
    
    pthread_mutex_unlock(&net_mutex);
    
    if (thread_to_join != 0) {
                        int join_result = pthread_join(thread_to_join, NULL);
        if (join_result != 0) {
                        logger_log(LOG_WARNING, "Failed to join receive thread: %s", strerror(join_result));
        } else {
            logger_log(LOG_INFO, "Stopped receive thread");
        }
    }
}

int net_handler_set_nickname(const char *nickname_str) {
    if (!nickname_str || strlen(nickname_str) < 2 || strlen(nickname_str) >= MAX_USERNAME_LEN) {
        logger_log(LOG_ERROR, "Invalid nickname: %s (must be 2-%d characters)", 
                  nickname_str ? nickname_str : "NULL", MAX_USERNAME_LEN - 1);
        return -1;
    }
    
    pthread_mutex_lock(&net_mutex);
    
    if (!connected || socket_fd == -1) {
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_ERROR, "Cannot set nickname - not connected to server");
        return -1;
    }
    
        NicknameRequest req;
    memset(&req, 0, sizeof(req));     
        strncpy(req.nickname, nickname_str, MAX_USERNAME_LEN - 1);
    req.nickname[MAX_USERNAME_LEN - 1] = '\0';
    
    logger_log(LOG_DEBUG, "Setting nickname to '%s' (length: %zu, struct size: %zu)", 
              req.nickname, strlen(req.nickname), sizeof(NicknameRequest));
    
        const int sock = socket_fd;
    pthread_mutex_unlock(&net_mutex);
    
        int result = send_message(sock, MSG_NICKNAME, &req, sizeof(req));
    
    if (result <= 0) {
        logger_log(LOG_ERROR, "Failed to send nickname request");
        return -1;
    }
    
        pthread_mutex_lock(&net_mutex);
    strncpy(nickname, nickname_str, MAX_USERNAME_LEN - 1);
    nickname[MAX_USERNAME_LEN - 1] = '\0';
    pthread_mutex_unlock(&net_mutex);
    
    logger_log(LOG_INFO, "Nickname request sent: %s", nickname_str);
    
    return 0;
}

int net_handler_send_message(const char *message) {
    if (!message) {
        logger_log(LOG_ERROR, "Cannot send NULL message");
        return -1;
    }
    
    pthread_mutex_lock(&net_mutex);
    
    if (!connected || socket_fd == -1 || !has_nickname) {
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_WARNING, "Not connected or no nickname set");
        return -1;
    }
    
                size_t username_len = strlen(nickname) + 1;     size_t message_len = strlen(message) + 1;       
        if (username_len > MAX_USERNAME_LEN)
        username_len = MAX_USERNAME_LEN;
    if (message_len > MAX_MESSAGE_LEN) {
        message_len = MAX_MESSAGE_LEN;
    }
    
        ChatMessage *msg = malloc(sizeof(ChatMessage));
    if (!msg) {
        pthread_mutex_unlock(&net_mutex);
        logger_log(LOG_ERROR, "Failed to allocate memory for chat message");
        return -1;
    }
    
        memset(msg, 0, sizeof(ChatMessage));
    
        strncpy(msg->username, nickname, MAX_USERNAME_LEN - 1);
    msg->username[MAX_USERNAME_LEN - 1] = '\0';
    
        strncpy(msg->message, message, MAX_MESSAGE_LEN - 1);
    msg->message[MAX_MESSAGE_LEN - 1] = '\0';
    
        logger_log(LOG_DEBUG, "Sending chat message: '%s', content size: %zu bytes, struct size: %zu bytes", 
              message, message_len, sizeof(ChatMessage));

        const int sock = socket_fd;
    pthread_mutex_unlock(&net_mutex);
    
        int result = send_message(sock, MSG_CHAT, msg, sizeof(ChatMessage));
    
        free(msg);
    
    if (result <= 0) {
        logger_log(LOG_ERROR, "Failed to send chat message");
        return -1;
    }
    
    return 0;
}

void net_handler_set_nickname_callback(const NicknameResponseCallback callback) {
    nickname_callback = callback;
}

void net_handler_set_chat_callback(ChatMessageCallback callback) {
    chat_callback = callback;
}

void net_handler_set_user_join_callback(const UserJoinCallback callback) {
    user_join_callback = callback;
}

void net_handler_set_user_leave_callback(const UserLeaveCallback callback) {
    user_leave_callback = callback;
}

void net_handler_set_user_list_callback(const UserListCallback callback) {
    user_list_callback = callback;
}

void net_handler_set_disconnect_callback(const DisconnectCallback callback) {
    disconnect_callback = callback;
}

int net_handler_is_connected(void) {
    pthread_mutex_lock(&net_mutex);
    const int result = connected;
    pthread_mutex_unlock(&net_mutex);
    return result;
}

int net_handler_has_nickname(void) {
    pthread_mutex_lock(&net_mutex);
    const int result = has_nickname;
    pthread_mutex_unlock(&net_mutex);
    return result;
}

const char *net_handler_get_nickname(void) {
    return nickname;
}

int net_handler_connect_with_nickname(const char *server_ip, const char *nickname_str) {
        if (!nickname_str || strlen(nickname_str) < 2) {
        logger_log(LOG_ERROR, "Invalid nickname: Too short (minimum 2 characters)");
        log_connection_error("Connection failed: Nickname too short (minimum 2 characters)");
        return -1;
    }
    
    if (strlen(nickname_str) >= MAX_USERNAME_LEN) {
        logger_log(LOG_ERROR, "Invalid nickname: Too long (maximum %d characters)", MAX_USERNAME_LEN - 1);
        log_connection_error("Connection failed: Nickname too long");
        return -1;
    }
    
        int result = net_handler_connect(server_ip);
    if (result != 0) {
                return result;
    }
    
        result = net_handler_set_nickname(nickname_str);
    if (result != 0) {
                log_connection_error("Failed to set nickname after connecting");
        net_handler_disconnect();
        return result;
    }
    
                
    return 0;
} 