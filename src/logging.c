#include "logging.h"
#include <assert.h>
#include <stdio.h>

int initialized = 0;
log_level logging_level;
FILE *log_target;

int init_logger(log_level level, char *log_filename) {
    logging_level = level;
    initialized = 1;
    if (log_filename) {
    } else {
        log_target = stdout;
    }
    return 1;
}

void do_log(char *level, char *format_string, ...) {
    fprintf(log_target, "%s ", level);
    va_list args;
    va_start(args, format_string);
    fprintf(log_target, format_string, args);
    va_end(args); 
}

void log_debug(char *format_string, ...) {
    assert(initialized);
    if (logging_level == DEBUG) {
        va_list args;
        va_start(args, format_string);
        do_log("[DEBUG]", format_string, args);
        va_end(args); 
    }
}

void log_warn(char *format_string, ...) {
    assert(initialized);
    va_list args;
    va_start(args, format_string);
    do_log("[WARN]", format_string, args);
    va_end(args); 
}
