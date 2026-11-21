#include "logger.h"

#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

static FILE *log_file = NULL;

int init_logger(void) {

    if (mkdir("logs", 0755) == -1 && errno != EEXIST) {
        perror("Failed to create logs directory");
        return -1;
    }

    log_file = fopen("logs/watchdog.log", "a");

    if (log_file == NULL) {
        perror("Failed to open log file");
        return -1;
    }

    int fd = fileno(log_file);
    if (fd == -1) {
        perror("Failed to get log file descriptor");
        fclose(log_file);
        log_file = NULL;
        return -1;
    }

    int flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        perror("Failed to set FD_CLOEXEC on log file");
        fclose(log_file);
        log_file = NULL;
        return -1;
    }

    return 0;
}

void log_message(const char *level, const char *message) {

    if (log_file == NULL) {
        return;
    }

    time_t current_time = time(NULL);
    struct tm local_time;

    if (localtime_r(&current_time, &local_time) == NULL) {
        return;
    }

    char timestamp[20];

    strftime(timestamp, sizeof(timestamp),
             "%Y-%m-%d %H:%M:%S", &local_time);

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