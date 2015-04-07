#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>

typedef enum {
    DEBUG, INFO, WARN, ERROR
} log_level;

int init_logger(log_level level, char *log_filename);
void log_debug(char *format_string, ...);
void log_info(char *format_string, ...);
void log_warn(char *format_string, ...);
void log_error(char *format_string, ...);

#endif
