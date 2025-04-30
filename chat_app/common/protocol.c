#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/socket.h>
#include <errno.h>
#include "protocol.h"
#include "logger.h"

static void __attribute__((constructor)) log_protocol_sizes(void) {
    logger_log(LOG_DEBUG, "Protocol structure sizes:");
    logger_log(LOG_DEBUG, "  MessageHeader:      %zu bytes", sizeof(MessageHeader));
    logger_log(LOG_DEBUG, "  NicknameRequest:    %zu bytes", sizeof(NicknameRequest));
    logger_log(LOG_DEBUG, "  NicknameResponse:   %zu bytes", sizeof(NicknameResponse));
    logger_log(LOG_DEBUG, "  ChatMessage:        %zu bytes", sizeof(ChatMessage));
    logger_log(LOG_DEBUG, "  UserNotification:   %zu bytes", sizeof(UserNotification));
    logger_log(LOG_DEBUG, "  RegisterRequest:    %zu bytes", sizeof(RegisterRequest));
    logger_log(LOG_DEBUG, "  RegisterResponse:   %zu bytes", sizeof(RegisterResponse));
    logger_log(LOG_DEBUG, "  LoginRequest:       %zu bytes", sizeof(LoginRequest));
    logger_log(LOG_DEBUG, "  LoginResponse:      %zu bytes", sizeof(LoginResponse));
}

int serialize_message(void *buffer, const MessageType type, const void *data, const __uint32_t data_length) {
    if (!buffer) {
        return -1;
    }
    
    memset(buffer, 0, sizeof(MessageHeader) + data_length);
    
    MessageHeader *header = (MessageHeader *)buffer;
    header->type = type;
    header->length = htonl(data_length);
    
    if (type == MSG_NICKNAME_RESPONSE) {
        NicknameResponse *resp = (NicknameResponse *)((uint8_t *)buffer + sizeof(MessageHeader));
        const NicknameResponse *orig = (NicknameResponse *)data;
        
        resp->status = orig->status;
        strncpy(resp->message, orig->message, MAX_MESSAGE_LEN);
        resp->message[MAX_MESSAGE_LEN - 1] = '\0';
        
        logger_log(LOG_DEBUG, "serialize_message: MSG_NICKNAME_RESPONSE, status=%d, message='%s', data_length=%u", 
                  orig->status, orig->message, data_length);
        
        return sizeof(MessageHeader) + data_length;
    }
    
    if (type == MSG_NICKNAME && data != NULL) {
        NicknameRequest *dest = (NicknameRequest *)((uint8_t *)buffer + sizeof(MessageHeader));
        const NicknameRequest *src = (const NicknameRequest *)data;
        
        strncpy(dest->nickname, src->nickname, MAX_USERNAME_LEN - 1);
        dest->nickname[MAX_USERNAME_LEN - 1] = '\0';
        
        logger_log(LOG_DEBUG, "serialize_message: MSG_NICKNAME, nickname='%s', length=%zu, data_length=%u", 
                  dest->nickname, strlen(dest->nickname), data_length);
        
        return sizeof(MessageHeader) + data_length;
    }
    
    if (type == MSG_CHAT && data != NULL) {
        ChatMessage *dest = (ChatMessage *)((uint8_t *)buffer + sizeof(MessageHeader));
        const ChatMessage *src = (const ChatMessage *)data;
        
        strncpy(dest->username, src->username, MAX_USERNAME_LEN - 1);
        dest->username[MAX_USERNAME_LEN - 1] = '\0';
        
        strncpy(dest->message, src->message, MAX_MESSAGE_LEN - 1);
        dest->message[MAX_MESSAGE_LEN - 1] = '\0';
        
        logger_log(LOG_DEBUG, "serialize_message: MSG_CHAT from '%s', message='%s', data_length=%u", 
                  dest->username, dest->message, data_length);
        
        return sizeof(MessageHeader) + sizeof(ChatMessage);
    }
    
    if (data && data_length > 0) {
        memcpy((uint8_t *)buffer + sizeof(MessageHeader), data, data_length);
    }
    
    logger_log(LOG_DEBUG, "serialize_message: type=%d, data_length=%u", type, data_length);
        
    return sizeof(MessageHeader) + data_length;
}

int deserialize_message(const void *buffer, MessageType *type, void *data, uint32_t *data_length) {
    if (!buffer || !type || !data_length) {
        return -1;
    }
    
    const MessageHeader *header = (const MessageHeader *)buffer;
    *type = header->type;
    *data_length = ntohl(header->length);
    
    logger_log(LOG_DEBUG, "deserialize_message: received type=%d, data_length=%u", *type, *data_length);
    
    if (data && *data_length > 0) {
        if (*type == MSG_NICKNAME_RESPONSE) {
            const NicknameResponse *src = (NicknameResponse *)((uint8_t *)buffer + sizeof(MessageHeader));
            NicknameResponse *dest = (NicknameResponse *)data;
            
            dest->status = src->status;
            strncpy(dest->message, src->message, MAX_MESSAGE_LEN);
            dest->message[MAX_MESSAGE_LEN - 1] = '\0';
            
            logger_log(LOG_DEBUG, "deserialize_message: MSG_NICKNAME_RESPONSE, status=%d, message='%s'", 
                      dest->status, dest->message);
        }
        else if (*type == MSG_NICKNAME) {
            const NicknameRequest *src = (NicknameRequest *)((uint8_t *)buffer + sizeof(MessageHeader));
            NicknameRequest *dest = (NicknameRequest *)data;
            
            strncpy(dest->nickname, src->nickname, MAX_USERNAME_LEN);
            dest->nickname[MAX_USERNAME_LEN - 1] = '\0';
            
            logger_log(LOG_DEBUG, "deserialize_message: MSG_NICKNAME, nickname='%s', length=%zu", 
                      dest->nickname, strlen(dest->nickname));
        }
        else {
            memcpy(data, (uint8_t *)buffer + sizeof(MessageHeader), *data_length);
        }
    }
    
    return sizeof(MessageHeader) + *data_length;
}

int send_message(int socket, MessageType type, const void *data, uint32_t data_length) {
    if (socket < 0) {
        logger_log(LOG_ERROR, "send_message: Invalid socket (%d)", socket);
        return -1;
    }
    
    if (type <= 0 || type > MSG_LOGIN_RESPONSE) {
        logger_log(LOG_ERROR, "send_message: Invalid message type (%d)", type);
        return -1;
    }
    
    if (data_length > 0 && data == NULL) {
        logger_log(LOG_ERROR, "send_message: data is NULL but length is %u", data_length);
        return -1;
    }
    
    const uint32_t MAX_ALLOWED_SIZE = 1024 * 1024;
    if (data_length > MAX_ALLOWED_SIZE) {
        logger_log(LOG_ERROR, "send_message: Message too large (%u bytes)", data_length);
        return -1;
    }
    
    uint8_t *buffer = malloc(sizeof(MessageHeader) + data_length);
    if (buffer == NULL) {
        logger_log(LOG_ERROR, "send_message: Failed to allocate memory for message buffer");
        return -1;
    }
    
    memset(buffer, 0, sizeof(MessageHeader) + data_length);
    
    switch (type) {
        case MSG_NICKNAME:
            if (data != NULL) {
                const NicknameRequest *req = (const NicknameRequest *)data;
                logger_log(LOG_DEBUG, "send_message: Sending MSG_NICKNAME, nickname='%s', length=%zu, data_length=%u", 
                          req->nickname, strlen(req->nickname), data_length);
            }
            break;
        case MSG_NICKNAME_RESPONSE:
            if (data != NULL) {
                const NicknameResponse *resp = (const NicknameResponse *)data;
                logger_log(LOG_DEBUG, "send_message: Sending MSG_NICKNAME_RESPONSE, status=%d, message='%s'", 
                          resp->status, resp->message);
            }
            break;
        default:
            logger_log(LOG_DEBUG, "send_message: Sending message type=%d, data_length=%u", type, data_length);
            break;
    }

    const int total_length = serialize_message(buffer, type, data, data_length);
    if (total_length < 0) {
        logger_log(LOG_ERROR, "send_message: Failed to serialize message (type=%d)", type);
        free(buffer);
        return -1;
    }

    const ssize_t bytes_sent = send(socket, buffer, total_length, 0);
    
    free(buffer);
    
    if (bytes_sent < 0) {
        logger_log(LOG_ERROR, "send_message: send() failed: %s", strerror(errno));
        return -1;
    }
    
    if (bytes_sent != total_length) {
        logger_log(LOG_WARNING, "send_message: Partial send, only %zd of %d bytes were sent", bytes_sent, total_length);
    }
    
    return bytes_sent;
}

int receive_message(const int socket, MessageType *type, void *data, uint32_t *data_length) {
    if (socket < 0 || !type || !data || !data_length) {
        return -1;
    }
    
    MessageHeader header;
    ssize_t bytes_received = recv(socket, &header, sizeof(header), MSG_WAITALL);
    
    if (bytes_received == 0) {
        logger_log(LOG_INFO, "receive_message: Connection closed by peer");
        return 0;
    }
    
    if (bytes_received < 0) {
        logger_log(LOG_ERROR, "receive_message: recv() failed: %s", strerror(errno));
        return -1;
    }
    
    if (bytes_received != sizeof(header)) {
        logger_log(LOG_ERROR, "receive_message: Received incomplete header (%zd bytes)", bytes_received);
        return -1;
    }
    
    *type = header.type;
    *data_length = ntohl(header.length);
    
    const uint32_t MAX_ALLOWED_SIZE = 1024 * 1024;
    if (*data_length > MAX_ALLOWED_SIZE) {
        logger_log(LOG_ERROR, "receive_message: Message too large (%u bytes)", *data_length);
        return -1;
    }
    
    logger_log(LOG_DEBUG, "receive_message: Received header with type=%d, length=%u", *type, *data_length);
    
    if (*data_length > 0) {
        bytes_received = recv(socket, data, *data_length, MSG_WAITALL);
        
        if (bytes_received == 0) {
            logger_log(LOG_INFO, "receive_message: Connection closed by peer while receiving data");
            return 0;
        }
        
        if (bytes_received < 0) {
            logger_log(LOG_ERROR, "receive_message: recv() failed while receiving data: %s", strerror(errno));
            return -1;
        }
        
        if ((uint32_t)bytes_received != *data_length) {
            logger_log(LOG_ERROR, "receive_message: Received incomplete data (%zd of %u bytes)", bytes_received, *data_length);
            return -1;
        }
        
        logger_log(LOG_DEBUG, "receive_message: Received %zd bytes of data", bytes_received);
        
        if (*type == MSG_CHAT) {
            ChatMessage *msg = (ChatMessage *)data;
            msg->username[MAX_USERNAME_LEN - 1] = '\0';
            msg->message[MAX_MESSAGE_LEN - 1] = '\0';
            logger_log(LOG_DEBUG, "receive_message: Chat message from '%s': '%s'", msg->username, msg->message);
        }
        else if (*type == MSG_NICKNAME) {
            NicknameRequest *req = (NicknameRequest *)data;
            req->nickname[MAX_USERNAME_LEN - 1] = '\0';
            logger_log(LOG_DEBUG, "receive_message: Nickname request: '%s'", req->nickname);
        }
    }
    
    return sizeof(header) + *data_length;
} 