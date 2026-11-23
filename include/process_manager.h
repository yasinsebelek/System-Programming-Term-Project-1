#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <sys/types.h>

#define WORKER_COUNT 2
#define MAX_RESTARTS 3

// Possible worker states
typedef enum {
    WORKER_STOPPED,
    WORKER_RUNNING,
    WORKER_RESTARTING,
    WORKER_FAILED
} WorkerStatus;

typedef struct {
    pid_t pid;
    int restart_count;
    WorkerStatus status;
} Worker;

int start_workers(void);

pid_t create_worker(void);

void check_children(void);

void restart_worker(Worker *worker, int worker_number);

void stop_workers(void);

void print_worker_status(void);

void periodic_check_workers(void);

int start_periodic_timer(unsigned int interval_seconds);

int stop_periodic_timer(void);

#endif

