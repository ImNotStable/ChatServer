/**
 * @file server_socket.c
 * @brief Socket handling functions for the chat server
 *
 * This file implements socket-related functions for the chat server,
 * including creation, binding, and connection acceptance.
 *
 * @author Jeremiah Hughes & Anthony Patton
 * @date April 2025
 */

#include "server_socket.h"
#include "../common/logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/**
 * @brief Creates and initializes a server socket
 *
 * This function creates a socket, sets socket options, binds the socket
 * to the specified port, and starts listening for connections.
 *
 * @param port The port number to listen on
 * @return The socket file descriptor on success, -1 on failure
 */
int create_server_socket(const int port) {
    struct sockaddr_in server_addr;
    const int opt = 1;
    
        const int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        logger_log(LOG_ERROR, "Failed to create socket: %s", strerror(errno));
        return -1;
    }
    
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger_log(LOG_ERROR, "Failed to set socket options: %s", strerror(errno));
        close(server_socket);
        return -1;
    }
    
        memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
        if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        logger_log(LOG_ERROR, "Failed to bind socket to port %d: %s", port, strerror(errno));
        close(server_socket);
        return -1;
    }
    
        if (listen(server_socket, 10) < 0) {
        logger_log(LOG_ERROR, "Failed to listen on socket: %s", strerror(errno));
        close(server_socket);
        return -1;
    }
    
    logger_log(LOG_INFO, "Server socket created and listening on port %d", port);
    return server_socket;
}

/**
 * @brief Accepts a client connection
 *
 * This function accepts a connection on the server socket and returns
 * the client socket. It also logs the client's IP address and port.
 *
 * @param server_socket The server socket file descriptor
 * @param client_ip Buffer to store the client's IP address
 * @param client_ip_size Size of the client_ip buffer
 * @param client_port Pointer to store the client's port
 * @return The client socket file descriptor on success, -1 on failure
 */
int accept_client_connection(const int server_socket, char *client_ip, const long unsigned int client_ip_size, int *client_port) {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

        const int client_socket = accept(server_socket, (struct sockaddr *) &client_addr, &client_addr_len);
    if (client_socket < 0) {
        logger_log(LOG_ERROR, "Failed to accept connection: %s", strerror(errno));
        return -1;
    }
    
        if (client_ip != NULL && client_ip_size > 0) {
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, client_ip_size);
    }
    
    if (client_port != NULL) {
        *client_port = ntohs(client_addr.sin_port);
    }
    
    logger_log(LOG_INFO, "Client connected from %s:%d", 
             client_ip != NULL ? client_ip : "unknown",
             client_port != NULL ? *client_port : 0);
    
    return client_socket;
}

/**
 * @brief Closes a socket
 *
 * This function safely closes a socket file descriptor.
 *
 * @param socket The socket file descriptor to close
 */
void close_socket(const int socket) {
    if (socket >= 0) {
        close(socket);
    }
} 