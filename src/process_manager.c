#include "process_manager.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define SHUTDOWN_TIMEOUT 5

// Stores information about all worker processes
static Worker *workers = NULL;

// Indicates whether the system is shutting down
static int shutting_down = 0;


/*
 * Creates a new child process and executes the worker program.
 */
pid_t create_worker(void) {

    // Flush output buffers before creating a child process
    fflush(NULL);

    pid_t pid = fork();

    if (pid < 0) {

        // Fork failed
        perror("fork failed");
        return -1;

    } else if (pid == 0) {

        // Child process

        // Execute the worker program
        execl("./bin/worker", "worker", (char *)NULL);

        // This code executes only if exec fails
        perror("exec failed");
        _exit(EXIT_FAILURE);

    } else {

        // Parent process
        printf("Worker created. PID: %d\n", pid);

    }

    return pid;
}


/*
 * Allocates memory and starts all worker processes.
 */
void start_workers(void) {

    // Prevent starting workers more than once
    if (workers != NULL) {
        return;
    }

    // Allocate memory for workers
    workers = malloc(sizeof(Worker) * WORKER_COUNT);

    if (workers == NULL) {
        perror("malloc failed");
        return;
    }

    // Initialize all worker structures
    for (int i = 0; i < WORKER_COUNT; i++) {

        workers[i].pid = -1;
        workers[i].restart_count = 0;
        workers[i].status = WORKER_STOPPED;
    }

    // Create all worker processes
    for (int i = 0; i < WORKER_COUNT; i++) {

        workers[i].pid = create_worker();

        if (workers[i].pid > 0) {

            workers[i].status = WORKER_RUNNING;

        } else {

            workers[i].status = WORKER_FAILED;
        }
    }

    printf("All workers have been started.\n");
}


/*
 * Restarts a terminated worker if the restart limit has not been reached.
 */
void restart_worker(Worker *worker) {

    // Check whether the maximum restart limit has been reached
    if (worker->restart_count >= MAX_RESTARTS) {

        printf("Worker reached maximum restart limit.\n");

        worker->status = WORKER_FAILED;
        worker->pid = -1;

        return;
    }

    // Update worker status
    worker->status = WORKER_RESTARTING;

    // Increase restart counter
    worker->restart_count++;

    printf("Restarting worker... Attempt: %d/%d\n",
           worker->restart_count, MAX_RESTARTS);

    // Create a new worker process
    worker->pid = create_worker();

    // Check whether the new worker was created successfully
    if (worker->pid > 0) {

        worker->status = WORKER_RUNNING;

        printf("Worker restarted successfully. PID: %d\n",
               worker->pid);

    } else {

        worker->status = WORKER_FAILED;

        printf("Worker restart failed.\n");
    }
}


/*
 * Checks whether any child process has terminated.
 * Restarts workers that terminate unexpectedly.
 */
void check_children(void) {

    // Make sure worker memory has been allocated
    if (workers == NULL || shutting_down) {
        return;
    }

    int status;
    pid_t pid;

    // Check for terminated child processes without blocking
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {

        // Find the worker that terminated
        for (int i = 0; i < WORKER_COUNT; i++) {

            if (workers[i].pid == pid) {

                printf("Worker %d (PID: %d) terminated.\n", i, pid);

                // Clear the old PID
                workers[i].pid = -1;

                // Check whether the worker exited normally
                if (WIFEXITED(status)) {

                    int exit_code = WEXITSTATUS(status);

                    if (exit_code == 0) {

                        printf("Worker exited normally.\n");

                        workers[i].status = WORKER_STOPPED;

                    } else {

                        printf("Worker exited with error. Code: %d\n",
                               exit_code);

                        restart_worker(&workers[i]);
                    }

                // Check whether the worker was terminated by a signal
                } else if (WIFSIGNALED(status)) {

                    printf("Worker terminated by signal: %d\n",
                           WTERMSIG(status));

                    restart_worker(&workers[i]);
                }

                break;
            }
        }
    }

    // Handle waitpid errors
    if (pid == -1 && errno != ECHILD && errno != EINTR) {
        perror("waitpid failed");
    }
}


/*
 * Stops all running workers gracefully.
 * Sends SIGTERM first and SIGKILL if the timeout expires.
 */
void stop_workers(void) {

    // Prevent restarting workers during shutdown
    shutting_down = 1;

    // Check whether worker memory exists
    if (workers == NULL) {
        return;
    }

    printf("Stopping all workers...\n");

    // Send SIGTERM to all running workers
    for (int i = 0; i < WORKER_COUNT; i++) {

        if (workers[i].pid > 0) {

            printf("Sending SIGTERM to worker PID: %d\n",
                   workers[i].pid);

            if (kill(workers[i].pid, SIGTERM) == -1 && errno != ESRCH) {
                perror("kill SIGTERM failed");
            }
        }
    }

    // Wait for workers to terminate gracefully
    for (int attempt = 0; attempt < SHUTDOWN_TIMEOUT; attempt++) {

        int running_workers = 0;

        for (int i = 0; i < WORKER_COUNT; i++) {

            if (workers[i].pid <= 0) {
                continue;
            }

            int status;
            pid_t result;

            do {
                result = waitpid(workers[i].pid, &status, WNOHANG);
            } while (result == -1 && errno == EINTR);

            if (result == workers[i].pid) {

                printf("Worker PID %d stopped gracefully.\n",
                       workers[i].pid);

                workers[i].pid = -1;
                workers[i].status = WORKER_STOPPED;

            } else if (result == 0) {

                running_workers++;

            } else if (result == -1 && errno == ECHILD) {

                workers[i].pid = -1;
                workers[i].status = WORKER_STOPPED;

            } else if (result == -1) {

                perror("waitpid failed");
            }
        }

        // All workers have terminated
        if (running_workers == 0) {
            break;
        }

        sleep(1);
    }

    // Forcefully terminate workers that did not stop
    for (int i = 0; i < WORKER_COUNT; i++) {

        if (workers[i].pid > 0) {

            printf("Worker PID %d did not stop. Sending SIGKILL.\n",
                   workers[i].pid);

            if (kill(workers[i].pid, SIGKILL) == -1 && errno != ESRCH) {
                perror("kill SIGKILL failed");
            }

            int status;
            pid_t result;

            // Collect the terminated child process
            do {
                result = waitpid(workers[i].pid, &status, 0);
            } while (result == -1 && errno == EINTR);

            workers[i].pid = -1;
            workers[i].status = WORKER_STOPPED;
        }
    }

    // Release dynamically allocated memory
    free(workers);
    workers = NULL;

    printf("All workers have been stopped.\n");
}
