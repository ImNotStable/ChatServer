# Makefile for Chat Server on Ubuntu
CC = gcc
CFLAGS = -Wall -Werror -std=c11 -D_GNU_SOURCE
SERVER_CFLAGS = $(CFLAGS) -DMAX_CLIENTS=100 -DSERVER_PORT=54321 -DBUFFER_SIZE=4096
CLIENT_CFLAGS = $(CFLAGS) -DMAX_USERNAME_LEN=32 -DBUFFER_SIZE=4096
BUILD_DIR = chat_app/build
COMMON_DIR = chat_app/common
CLIENT_DIR = chat_app/client
SERVER_DIR = chat_app/server

# Include GTK3 flags
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-3.0)

# Define targets
.PHONY: all clean server client install

all: server client

# Common library
common: $(BUILD_DIR)/libcommon.a

$(BUILD_DIR)/libcommon.a: $(wildcard $(COMMON_DIR)/*.c)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -DMAX_USERNAME_LEN=32 -DMAX_PASSWORD_LEN=64 -DSERVER_PORT=54321 -DBUFFER_SIZE=4096 -c $(COMMON_DIR)/logger.c -o $(BUILD_DIR)/logger.o
	$(CC) $(CFLAGS) -DMAX_USERNAME_LEN=32 -DMAX_PASSWORD_LEN=64 -DSERVER_PORT=54321 -DBUFFER_SIZE=4096 -c $(COMMON_DIR)/protocol.c -o $(BUILD_DIR)/protocol.o
	ar rcs $(BUILD_DIR)/libcommon.a $(BUILD_DIR)/logger.o $(BUILD_DIR)/protocol.o

# Server target
server: common $(BUILD_DIR)/server

$(BUILD_DIR)/server: $(wildcard $(SERVER_DIR)/*.c)
	@mkdir -p $(BUILD_DIR)/server
	$(CC) $(SERVER_CFLAGS) -I$(COMMON_DIR) $(SERVER_DIR)/server.c $(SERVER_DIR)/chat_handler.c $(SERVER_DIR)/server_socket.c $(BUILD_DIR)/libcommon.a -o $(BUILD_DIR)/server/server -lpthread

# Client target
client: common $(BUILD_DIR)/client

$(BUILD_DIR)/client: $(wildcard $(CLIENT_DIR)/*.c)
	@mkdir -p $(BUILD_DIR)/client
	$(CC) $(CLIENT_CFLAGS) $(GTK_CFLAGS) -I$(COMMON_DIR) $(CLIENT_DIR)/client.c $(CLIENT_DIR)/gui.c $(CLIENT_DIR)/net_handler.c $(BUILD_DIR)/libcommon.a -o $(BUILD_DIR)/client/client $(GTK_LIBS) -lpthread

# Clean target
clean:
	rm -rf $(BUILD_DIR)

# Install target
install: all
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(BUILD_DIR)/server/server $(DESTDIR)/usr/local/bin/chat-server
	install -m 755 $(BUILD_DIR)/client/client $(DESTDIR)/usr/local/bin/chat-client

# Run targets
run-server: server
	$(BUILD_DIR)/server/server

run-client: client
	$(BUILD_DIR)/client/client 