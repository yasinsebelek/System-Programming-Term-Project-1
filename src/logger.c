#include "logger.h"

#include <stdio.h>
#include <time.h>

static FILE *log_file = NULL;

int init_logger(void) {

    log_file = fopen("logs/watchdog.log", "a");

    if (log_file == NULL) {
        perror("Failed to open log file");
        return -1;
    }

    return 0;
}

void log_message(const char *level, const char *message) {

    if (log_file == NULL) {
        return;
    }

    time_t current_time = time(NULL);

    struct tm *local_time = localtime(&current_time);

    if (local_time == NULL) {
        return;
    }

    char timestamp[20];

    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S", local_time);

    fprintf(log_file, "[%s] [%s] %s\n",
            timestamp, level, message);

    fflush(log_file);
}

void close_logger(void) {

    if (log_file != NULL) {

        fclose(log_file);
        log_file = NULL;
    }
}



