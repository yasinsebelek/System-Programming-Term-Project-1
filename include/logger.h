#ifndef LOGGER_H
#define LOGGER_H

#include <sys/types.h>

int init_logger(void);

void log_message(const char *level, const char *message);

void log_worker_status(int worker_number,
                       pid_t pid,
                       const char *status,
                       int restart_count,
                       int max_restarts);

void close_logger(void);

#endif
