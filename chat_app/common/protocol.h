#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#ifndef SERVER_PORT
#define SERVER_PORT 54321
#endif

#ifndef MAX_USERNAME_LEN
#define MAX_USERNAME_LEN 32
#endif

#ifndef MAX_MESSAGE_LEN
#define MAX_MESSAGE_LEN 1024
#endif

#ifndef MAX_PASSWORD_LEN
#define MAX_PASSWORD_LEN 64
#endif

typedef enum {
    MSG_NICKNAME = 1,
    MSG_NICKNAME_RESPONSE,
    MSG_CHAT,
    MSG_USER_JOIN,
    MSG_USER_LEAVE,
    MSG_USER_LIST,
    MSG_DISCONNECT,
    MSG_REGISTER,
    MSG_REGISTER_RESPONSE,
    MSG_LOGIN,
    MSG_LOGIN_RESPONSE
} MessageType;

typedef enum {
    STATUS_SUCCESS = 0,
    STATUS_ERROR,
    STATUS_NICKNAME_TAKEN,
    STATUS_INVALID_CREDENTIALS,
    STATUS_USER_LOGGED_IN,
    STATUS_USER_EXISTS
} StatusCode;

typedef struct {
    uint8_t type;
    uint32_t length;
} MessageHeader;

typedef struct {
    char nickname[MAX_USERNAME_LEN];
} NicknameRequest;

typedef struct {
    uint8_t status;
    char message[MAX_MESSAGE_LEN];
} NicknameResponse;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char message[MAX_MESSAGE_LEN];
} ChatMessage;

typedef struct {
    char username[MAX_USERNAME_LEN];
} UserNotification;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
} RegisterRequest;

typedef struct {
    uint8_t status;
    char message[MAX_MESSAGE_LEN];
} RegisterResponse;

typedef struct {
    char username[MAX_USERNAME_LEN];
    char password[MAX_PASSWORD_LEN];
} LoginRequest;

typedef struct {
    uint8_t status;
    char message[MAX_MESSAGE_LEN];
} LoginResponse;

int serialize_message(void *buffer, MessageType type, const void *data, uint32_t data_length);
int deserialize_message(const void *buffer, MessageType *type, void *data, uint32_t *data_length);
int send_message(int socket, MessageType type, const void *data, uint32_t data_length);
int receive_message(int socket, MessageType *type, void *data, uint32_t *data_length);

#endif