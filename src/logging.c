#include <assert.h>
#include <stdio.h>
#include <sys/stat.h> 
#include <fcntl.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include "logging.h"

int initialized = 0;
log_level logging_level;
FILE *log_target;
char *target_filename = NULL;

int init_logger(log_level level, char *log_filename) {
    logging_level = level;
    initialized = 1;
    if (log_filename) {
        target_filename = strdup(log_filename);
        log_target = fopen(log_filename, "a");
        if (!log_target) {
            perror("Failed to open log file");
            return 0;
        }
        printf("Logging to %s...\n", log_filename);
    } else {
        log_target = stdout;
    }
    return 1;
}

void do_log(char *level, char *format_string, va_list args) {
    time_t msg_time;
    struct tm *tm_p;
    msg_time = time(NULL);
    tm_p = localtime(&msg_time);
    fprintf(log_target, "%.2d:%.2d:%.2d %s ", tm_p->tm_hour, tm_p->tm_min, tm_p->tm_sec, level); 
    vfprintf(log_target, format_string, args);
    fflush(log_target);
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

void log_info(char *format_string, ...) {
    assert(initialized);
    if (logging_level == DEBUG || logging_level == INFO) {
        va_list args;
        va_start(args, format_string);
        do_log("[INFO]", format_string, args);
        va_end(args); 
    }
}

void log_warn(char *format_string, ...) {
    assert(initialized);
    if (logging_level == DEBUG || logging_level == INFO || logging_level == WARN) {
        va_list args;
        va_start(args, format_string);
        do_log("[WARN]", format_string, args);
        va_end(args); 
    }
}

void log_error(char *format_string, ...) {
    assert(initialized);
    va_list args;
    va_start(args, format_string);
    do_log("[ERROR]", format_string, args);
    va_end(args); 
}

void log_reopen() {
    assert(target_filename);
    log_info("Reopening the log file\n");
    // ignore the return value as we can't even log the error anywhere!
    fclose(log_target); 
    
    log_target = fopen(target_filename, "a");
    if (!log_target) {
        // we can only exit, no way to write this error anywhere
        exit(3);
    }
}
