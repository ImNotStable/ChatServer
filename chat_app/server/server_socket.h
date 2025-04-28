#ifndef SERVER_SOCKET_H
#define SERVER_SOCKET_H

#include <stddef.h>

int create_server_socket(int port);
int accept_client_connection(int server_socket, char *client_ip, size_t client_ip_size, int *client_port);
void close_socket(int socket);

#endif