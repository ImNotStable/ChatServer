#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include "../common/protocol.h"

int gui_init(const int *argc, char ***argv);
void gui_cleanup(void);

void gui_main(void);
void gui_show_main_window(void);
void gui_hide_main_window(void);

void gui_add_chat_message(const char *username, const char *message);
void gui_add_system_message(const char *message);
void gui_clear_chat(void);

void gui_update_user_list(const char *user_list, const int length);
void gui_add_user(const char *username);
void gui_remove_user(const char *username);

void gui_show_connect_dialog(void);
void gui_show_nickname_dialog(void);
void gui_show_error(const char *title, const char *message);
void gui_show_info(const char *title, const char *message);

int gui_init_fallback(void);
void gui_main_fallback(void);
int gui_is_fallback_mode(void);
void gui_add_chat_message_fallback(const char *username, const char *message);
void gui_add_system_message_fallback(const char *message);

#endif