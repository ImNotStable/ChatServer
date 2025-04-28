#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui.h"
#include "net_handler.h"
#include "../common/logger.h"

#define LOG_FILE "client.log"

int init_client(int argc, char **argv) {
    if (logger_init(LOG_FILE) != 0) {
        fprintf(stderr, "Failed to initialize logger\n");
        return -1;
    }

    logger_log(LOG_INFO, "Chat client starting up");

    logger_log(LOG_DEBUG, "Protocol structure sizes (client):");
    logger_log(LOG_DEBUG, "  MessageHeader:      %zu bytes", sizeof(MessageHeader));
    logger_log(LOG_DEBUG, "  NicknameRequest:    %zu bytes", sizeof(NicknameRequest));
    logger_log(LOG_DEBUG, "  NicknameResponse:   %zu bytes", sizeof(NicknameResponse));
    logger_log(LOG_DEBUG, "  ChatMessage:        %zu bytes", sizeof(ChatMessage));
    logger_log(LOG_DEBUG, "  UserNotification:   %zu bytes", sizeof(UserNotification));

    if (net_handler_init() != 0) {
        logger_log(LOG_ERROR, "Failed to initialize network handler");
        return -1;
    }

    if (gui_init(&argc, &argv) != 0) {
        logger_log(LOG_ERROR, "Failed to initialize GUI");
        return -1;
    }

    return 0;
}

void cleanup_client() {
    gui_cleanup();
    net_handler_disconnect();
    logger_log(LOG_INFO, "Client shutdown complete");
    logger_close();
}

void run_client() {
    if (gui_is_fallback_mode()) {
        logger_log(LOG_INFO, "Running in fallback mode (text-only)");
        gui_main_fallback();
    } else {
        logger_log(LOG_INFO, "Running in GUI mode");
        gui_main();
    }
}

int main(const int argc, char **argv) {
    setenv("GIO_USE_DBUS", "no", 1);
    setenv("GIO_USE_VFS", "local", 1);
    setenv("GSETTINGS_BACKEND", "memory", 1);
    setenv("GDK_BACKEND", "x11", 1);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fallback") == 0) {
            setenv("GTK_DEBUG", "force-fallback", 1);
            break;
        }
    }

    if (init_client(argc, argv) != 0) {
        fprintf(stderr, "Failed to initialize client\n");
        return 1;
    }

    run_client();

    cleanup_client();

    return 0;
}
