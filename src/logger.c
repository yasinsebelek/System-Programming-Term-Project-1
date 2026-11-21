#include "logger.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_LOG_FILE_SIZE (1024 * 1024) /* 1 MB */
#define TEST_LOG_PATH "logs/watchdog_test.log"

static FILE *log_file = NULL;
static int is_test_mode = 0;
static char current_date[11] = ""; /* YYYY-MM-DD */

static FILE *open_log_file(const char *path) {

    FILE *fp = fopen(path, "a");
    if (fp == NULL) {
        perror("Failed to open log file");
        return NULL;
    }

    int fd = fileno(fp);
    if (fd == -1) {
        perror("Failed to get log file descriptor");
        fclose(fp);
        return NULL;
    }

    int flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        perror("Failed to set FD_CLOEXEC on log file");
        fclose(fp);
        return NULL;
    }

    return fp;
}

static int open_daily_log(const char *date_str) {

    char path[128];

    for (int part = 0; part < 10000; part++) {

        if (part == 0) {
            snprintf(path, sizeof(path), "logs/watchdog_%s.log", date_str);
        } else {
            snprintf(path, sizeof(path), "logs/watchdog_%s_%d.log", date_str, part);
        }

        struct stat st;
        if (stat(path, &st) == 0 && st.st_size >= (off_t)MAX_LOG_FILE_SIZE) {
            continue;
        }

        FILE *new_fp = open_log_file(path);
        if (new_fp == NULL) {
            return -1;
        }

        if (log_file != NULL) {
            fclose(log_file);
        }

        log_file = new_fp;
        snprintf(current_date, sizeof(current_date), "%s", date_str);
        return 0;
    }

    fprintf(stderr, "Failed to find available daily log file for %s\n", date_str);
    return -1;
}

int init_logger(void) {

    if (mkdir("logs", 0755) == -1 && errno != EEXIST) {
        perror("Failed to create logs directory");
        return -1;
    }

    const char *test_mode = getenv("WATCHDOG_TEST_MODE");
    if (test_mode != NULL && strcmp(test_mode, "1") == 0) {
        is_test_mode = 1;
        log_file = open_log_file(TEST_LOG_PATH);
        return (log_file != NULL) ? 0 : -1;
    }

    is_test_mode = 0;

    time_t now = time(NULL);
    struct tm local_time;

    if (localtime_r(&now, &local_time) == NULL) {
        return -1;
    }

    char date_str[11];
    strftime(date_str, sizeof(date_str), "%Y-%m-%d", &local_time);

    return open_daily_log(date_str);
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

    if (!is_test_mode) {
        char today[11];
        strftime(today, sizeof(today), "%Y-%m-%d", &local_time);

        int need_rotate = (strcmp(today, current_date) != 0);

        if (!need_rotate) {
            struct stat st;
            int fd = fileno(log_file);
            if (fd != -1 && fstat(fd, &st) == 0 && st.st_size >= (off_t)MAX_LOG_FILE_SIZE) {
                need_rotate = 1;
            }
        }

        if (need_rotate) {
            if (open_daily_log(today) == -1) {
                return;
            }
        }
    }

    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_time);

    fprintf(log_file, "[%s] [%s] %s\n", timestamp, level, message);
    fflush(log_file);
}

void close_logger(void) {

    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
    current_date[0] = '\0';
    is_test_mode = 0;
}