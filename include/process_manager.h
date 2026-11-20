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

// Stores information about each worker
typedef struct {
    pid_t pid;
    int restart_count;
    WorkerStatus status;
} Worker;

// Starts all child worker processes
void start_workers(void);

// Creates a single child process
pid_t create_worker(void);

// Checks terminated child processes
void check_children(void);

// Restarts a worker using its address
void restart_worker(Worker *worker);

// Stops all running workers
void stop_workers(void);

#endif
