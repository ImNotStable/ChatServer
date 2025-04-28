#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

typedef enum {
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_DEBUG
} LogType;

int logger_init(const char *filename);
void logger_close(void);
void logger_log(LogType type, const char *format, ...);
const char *log_type_str(LogType type);

#endif