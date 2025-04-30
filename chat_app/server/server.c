/**
 * @file server.c
 * @brief Main server implementation for the chat application
 *
 * This file implements the server-side functionality of the chat application,
 * including socket handling, client connections, and message processing.
 *
 * @author Jeremiah Hughes & Anthony Patton
 * @date April 2025
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "chat_handler.h"
#include "../common/logger.h"
#include "../common/protocol.h"

#define LOG_FILE "server.log"
#define CHAT_BUFFER_SIZE 8192
#define SERVER_PORT 54321

static int server_socket = -1;
static int running = 1;
static int active_users = 0;

static pthread_mutex_t active_users_mutex = PTHREAD_MUTEX_INITIALIZER;

extern pthread_mutex_t clients_mutex;
extern Client *clients[MAX_CLIENTS];

int handle_client_connection(int client_socket);
int process_message(int client_id, const char *message);
int process_command(int client_id, const char *command);
int chat_handler_set_nickname(int client_id, const char *nickname);
int chat_handler_get_nickname(int client_id, char *nickname_buf);
int chat_handler_send_message(int client_id, const char *message);
void chat_handler_get_online_users(char *buffer, size_t buffer_size);

void broadcast_message(MessageType type, const void *data, uint32_t data_length, int exclude_socket);
void send_user_list(int client_socket);

void handle_signal(const int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        logger_log(LOG_INFO, "Received signal %d, shutting down...", sig);
        running = 0;

        if (server_socket != -1) {
            close(server_socket);
            server_socket = -1;
        }
    }
}

/**
 * @brief Initialize the server
 *
 * Sets up the server by initializing the logger and network components.
 *
 * @param port The port number to listen on
 * @return 0 on success, non-zero on failure
 */
int server_init(const int port) {
        if (logger_init(LOG_FILE) != 0) {
            fprintf(stderr, "Failed to initialize logger\n");
        return -1;
    }
    
    logger_log(LOG_INFO, "Chat server starting up");

        if (chat_handler_init() != 0) {
            logger_log(LOG_ERROR, "Failed to initialize chat handler");
        return -1;
    }
    
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket < 0) {
        logger_log(LOG_ERROR, "Failed to create server socket");
        return -1;
    }

        const int opt = 1;
        if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        logger_log(LOG_ERROR, "Failed to set socket options");
        close(server_socket);
        return -1;
    }

        struct sockaddr_in server_addr = {0};
        server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        logger_log(LOG_ERROR, "Failed to bind server socket");
        close(server_socket);
        return -1;
    }

        if (listen(server_socket, 10) < 0) {
            logger_log(LOG_ERROR, "Failed to listen on server socket");
        close(server_socket);
        return -1;
    }
    
    logger_log(LOG_INFO, "Server initialized and listening on port %d", port);
    return 0;
}

/**
 * @brief Shut down the server
 *
 * Releases all resources and properly terminates the server.
 */
void server_shutdown(void) {
        if (server_socket != -1) {
            close(server_socket);
        server_socket = -1;
    }

        chat_handler_cleanup();
        logger_log(LOG_INFO, "Server shutdown complete");
    logger_close();
}

/**
 * @brief Start the server main loop
 *
 * Starts listening for client connections and handles them accordingly.
 * This function blocks until the server is shut down.
 *
 * @return 0 on successful shutdown, non-zero on error
 */
int server_run(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        const int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) {
            if (running) {
                logger_log(LOG_ERROR, "Failed to accept client connection");
            }
            continue;
        }
        
                char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        logger_log(LOG_INFO, "New client connection from %s:%d", 
                 client_ip, ntohs(client_addr.sin_port));
        
                const int client_id = chat_handler_add_client(client_socket);
        if (client_id < 0) {
            logger_log(LOG_ERROR, "Failed to add client to chat handler");
            close(client_socket);
            continue;
        }
        
                pthread_mutex_lock(&active_users_mutex);
        active_users++;
        pthread_mutex_unlock(&active_users_mutex);
        
        logger_log(LOG_INFO, "Client %d added successfully. Active clients: %d", 
                 client_id, active_users);
    }

    return 0;
}

/**
 * @brief Main function
 *
 * Entry point for the chat server application.
 *
 * @param argc Number of command line arguments
 * @param argv Command line arguments
 * @return 0 on successful execution, non-zero on error
 */
int main(const int argc, char *argv[]) {
    int port = SERVER_PORT;
    
        if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %s\n", argv[1]);
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            return EXIT_FAILURE;
        }
    }
    
        if (server_init(port) != 0) {
        fprintf(stderr, "Failed to initialize server\n");
        return EXIT_FAILURE;
    }
    
        const int result = server_run();
    
        server_shutdown();

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
