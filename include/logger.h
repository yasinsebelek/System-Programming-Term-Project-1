#ifndef LOGGER_H
#define LOGGER_H

int init_logger(void);

void log_message(const char *level, const char *message);

void close_logger(void);

#endif