#ifndef LOGGING_H
#define LOGGING_H

#include <stdarg.h>

typedef enum {
    DEBUG, INFO, WARN, ERROR
} log_level;

// creates a logger with the given level. log_filename can be NULL or a name of a file
int init_logger(log_level level, char *log_filename);
// closes the log file and opens it again
void log_reopen();

// log functions for different log levels
void log_debug(char *format_string, ...);
void log_info(char *format_string, ...);
void log_warn(char *format_string, ...);
void log_error(char *format_string, ...);

#endif
