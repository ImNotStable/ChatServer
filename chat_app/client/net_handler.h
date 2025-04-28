#ifndef NET_HANDLER_H
#define NET_HANDLER_H

#include <pthread.h>
#include "../common/protocol.h"

typedef void (*NicknameResponseCallback)(NicknameResponse *response);
typedef void (*ChatMessageCallback)(const ChatMessage *message);
typedef void (*UserJoinCallback)(UserNotification *notification);
typedef void (*UserLeaveCallback)(UserNotification *notification);
typedef void (*UserListCallback)(const char *user_list, int length);
typedef void (*DisconnectCallback)(void);

int net_handler_init(void);
int net_handler_connect(const char *server_ip);
int net_handler_connect_with_nickname(const char *server_ip, const char *nickname_str);
void net_handler_disconnect(void);
int net_handler_start_receiving(void);
void net_handler_stop_receiving(void);
int net_handler_set_nickname(const char *nickname);
int net_handler_send_message(const char *message);

void net_handler_set_nickname_callback(NicknameResponseCallback callback);
void net_handler_set_chat_callback(ChatMessageCallback callback);
void net_handler_set_user_join_callback(UserJoinCallback callback);
void net_handler_set_user_leave_callback(UserLeaveCallback callback);
void net_handler_set_user_list_callback(UserListCallback callback);
void net_handler_set_disconnect_callback(DisconnectCallback callback);

int net_handler_is_connected(void);
int net_handler_has_nickname(void);
const char *net_handler_get_nickname(void);

#endif