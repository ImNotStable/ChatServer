#ifndef CHAT_HANDLER_H
#define CHAT_HANDLER_H

#include <pthread.h>
#include "../common/protocol.h"

typedef struct {
    int socket;
    int id;
    char nickname[MAX_USERNAME_LEN];
    int has_nickname;
    pthread_t thread;
} Client;

int chat_handler_init(void);
void chat_handler_cleanup(void);
int chat_handler_add_client(int client_socket);
void chat_handler_remove_client(int client_id);
void *chat_handler_client_thread(void *arg);
int chat_handler_is_nickname_taken(const char *nickname);
void chat_handler_broadcast_message(const char *sender, const char *message);
void chat_handler_user_joined(const char *nickname);
void chat_handler_user_left(const char *nickname);
void chat_handler_send_user_list(int client_socket);
int chat_handler_set_nickname(int client_id, const char *nickname);
int chat_handler_get_nickname(int client_id, char *nickname_buf);
int chat_handler_send_message(int client_id, const char *message);
void chat_handler_get_online_users(char *buffer, size_t buffer_size);
void broadcast_message(MessageType type, const void *data, uint32_t data_length, int exclude_socket);
void send_user_list(int client_socket);

#endif 