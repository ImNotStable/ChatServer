#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include "logger.h"

static FILE *log_file = NULL;

int logger_init(const char *filename) {
    if (log_file != NULL) {
        return 0;
    }
    
    log_file = fopen(filename, "a");
    if (log_file == NULL) {
        perror("Failed to open log file");
        return -1;
    }
    
    return 0;
}

void logger_close(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

const char *log_type_str(const LogType type) {
    switch (type) {
        case LOG_INFO:    return "INFO";
        case LOG_WARNING: return "WARNING";
        case LOG_ERROR:   return "ERROR";
        case LOG_DEBUG:   return "DEBUG";
        default:          return "UNKNOWN";
    }
}

void logger_log(const LogType type, const char *format, ...) {
    if (log_file == NULL) {
        return;
    }
    
    const time_t now = time(NULL);
    const struct tm *tm_info = localtime(&now);
    char time_str[20];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(log_file, "[%s] [%s] ", time_str, log_type_str(type));
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
    
    fprintf(stdout, "[%s] [%s] ", time_str, log_type_str(type));
    
    va_start(args, format);
    vfprintf(stdout, format, args);
    va_end(args);
    
    fprintf(stdout, "\n");
    fflush(stdout);
}