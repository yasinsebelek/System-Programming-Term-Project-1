#include "process_manager.h"
#include "logger.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>

#define SHUTDOWN_TIMEOUT 5

static Worker *workers = NULL;
static int shutting_down = 0;


pid_t create_worker(void) {

    int error_pipe[2];

    if (pipe(error_pipe) == -1) {

        int error_code = errno;
        perror("[ERROR] pipe failed");

        char message[150];

        snprintf(message, sizeof(message),
                 "Failed to create error pipe | Error: %d",
                 error_code);

        log_message("ERROR", message);

        return -1;
    }

    
    int flags = fcntl(error_pipe[1], F_GETFD);

    if (flags == -1 ||
        fcntl(error_pipe[1], F_SETFD, flags | FD_CLOEXEC) == -1) {

        int error_code = errno;

        close(error_pipe[0]);
        close(error_pipe[1]);

        char message[150];

        snprintf(message, sizeof(message),
                 "Failed to configure error pipe | Error: %d",
                 error_code);

        log_message("ERROR", message);

        return -1;
    }

    fflush(NULL);

    pid_t pid = fork();

    if (pid < 0) {

        int error_code = errno;

        close(error_pipe[0]);
        close(error_pipe[1]);

        perror("[ERROR] fork failed");

        char message[150];

        snprintf(message, sizeof(message),
                 "Failed to create child process | Error: %d",
                 error_code);

        log_message("ERROR", message);

        return -1;
    }

    if (pid == 0) {

        close(error_pipe[0]);

        execl("./bin/worker", "worker", (char *)NULL);

        int error_code = errno;

        /*
         * Notify parent that exec failed.
         */
        ssize_t written;

        do {
            written = write(error_pipe[1],
                            &error_code,
                            sizeof(error_code));
        } while (written == -1 && errno == EINTR);

        _exit(EXIT_FAILURE);
    }

    close(error_pipe[1]);

    int exec_error = 0;
    ssize_t bytes_read;

    do {
        bytes_read = read(error_pipe[0],
                          &exec_error,
                          sizeof(exec_error));
    } while (bytes_read == -1 && errno == EINTR);

    close(error_pipe[0]);

    if (bytes_read > 0) {

        int status;
        pid_t result;

        do {
            result = waitpid(pid, &status, 0);
        } while (result == -1 && errno == EINTR);

        char message[150];

        snprintf(message, sizeof(message),
                 "exec() failed for child process | PID: %d | Error: %d",
                 pid, exec_error);

        log_message("ERROR", message);

        fprintf(stderr, "[ERROR] exec failed: %s\n",
                strerror(exec_error));

        return -1;
    }

    if (bytes_read == -1) {

        int error_code = errno;

        kill(pid, SIGKILL);

        int status;

        while (waitpid(pid, &status, 0) == -1 && errno == EINTR) {
        }

        char message[150];

        snprintf(message, sizeof(message),
                 "Failed to read exec status | PID: %d | Error: %d",
                 pid, error_code);

        log_message("ERROR", message);

        return -1;
    }

    printf("[INFO] Child process created | PID: %d\n", pid);

    char message[100];

    snprintf(message, sizeof(message),
             "Child process created | PID: %d",
             pid);

    log_message("INFO", message);

    return pid;
}

int start_workers(void) {

    if (workers != NULL) {
        return 0;
    }

    workers = malloc(sizeof(Worker) * WORKER_COUNT);

    if (workers == NULL) {

        perror("[ERROR] malloc failed");

        log_message("ERROR",
                    "Failed to allocate memory for workers.");

        return -1;
    }

    for (int i = 0; i < WORKER_COUNT; i++) {

        workers[i].pid = -1;
        workers[i].restart_count = 0;
        workers[i].status = WORKER_STOPPED;
    }

    for (int i = 0; i < WORKER_COUNT; i++) {

        workers[i].pid = create_worker();

        if (workers[i].pid > 0) {

            workers[i].status = WORKER_RUNNING;

            printf("[SUCCESS] Worker #%d created | PID: %d\n",
                   i + 1, workers[i].pid);

        } else {

            workers[i].status = WORKER_FAILED;

            fprintf(stderr,
                    "\n[ERROR] Failed to create Worker #%d.\n"
                    "[WARNING] Rolling back worker startup...\n",
                    i + 1);

            char message[150];

            snprintf(message, sizeof(message),
                     "Failed to create Worker #%d. Rolling back startup.",
                     i + 1);

            log_message("ERROR", message);

            stop_workers();

            return -1;
        }
    }

    printf("\n[SUCCESS] All workers initialized successfully.\n");

    log_message("SUCCESS",
                "All workers initialized successfully.");

    return 0;
}

void restart_worker(Worker *worker, int worker_number) {

    if (worker->restart_count >= MAX_RESTARTS) {

        printf("\n[ERROR] Worker #%d reached maximum restart limit (%d).\n",
               worker_number, MAX_RESTARTS);

        char message[150];

        snprintf(message, sizeof(message),
                 "Worker #%d reached maximum restart limit (%d).",
                 worker_number, MAX_RESTARTS);

        log_message("ERROR", message);

        worker->status = WORKER_FAILED;
        worker->pid = -1;

        return;
    }

    worker->status = WORKER_RESTARTING;

    worker->restart_count++;

    printf("\n[INFO] Restarting Worker #%d | Attempt: %d/%d\n",
           worker_number,
           worker->restart_count,
           MAX_RESTARTS);

    char message[150];

    snprintf(message, sizeof(message),
             "Restarting Worker #%d | Attempt: %d/%d",
             worker_number,
             worker->restart_count,
             MAX_RESTARTS);

    log_message("WARNING", message);

    worker->pid = create_worker();

    if (worker->pid > 0) {

        worker->status = WORKER_RUNNING;

        printf("[SUCCESS] Worker #%d restarted successfully.\n",
               worker_number);

        printf("[INFO] New PID: %d\n", worker->pid);

        char success_message[150];

        snprintf(success_message, sizeof(success_message),
                 "Worker #%d restarted successfully | New PID: %d",
                 worker_number, worker->pid);

        log_message("SUCCESS", success_message);

    } else {

        worker->status = WORKER_FAILED;

        printf("[ERROR] Worker #%d restart failed.\n",
               worker_number);

        char error_message[100];

        snprintf(error_message, sizeof(error_message),
                 "Worker #%d restart failed.",
                 worker_number);

        log_message("ERROR", error_message);
    }
}

void check_children(void) {

    if (workers == NULL || shutting_down) {
        return;
    }

    int status;
    pid_t pid;

    while (1) {

        pid = waitpid(-1, &status, WNOHANG);

        if (pid > 0) {

            for (int i = 0; i < WORKER_COUNT; i++) {

                if (workers[i].pid == pid) {

                    printf("\n[WARNING] Worker #%d terminated | PID: %d\n",
                           i + 1, pid);

                    char message[150];

                    snprintf(message, sizeof(message),
                             "Worker #%d terminated | PID: %d",
                             i + 1, pid);

                    log_message("WARNING", message);

                    workers[i].pid = -1;

                    if (WIFEXITED(status)) {

                        int exit_code = WEXITSTATUS(status);

                        if (exit_code == 0) {

                            printf("[INFO] Worker #%d exited normally.\n",
                                   i + 1);

                            log_message("INFO",
                                        "Worker exited normally with exit code 0.");

                            workers[i].status = WORKER_STOPPED;

                        } else {

                            char error_message[150];

                            snprintf(error_message, sizeof(error_message),
                                     "Worker #%d exited with error | Exit code: %d",
                                     i + 1, exit_code);

                            printf("[ERROR] %s\n", error_message);

                            log_message("ERROR", error_message);

                            restart_worker(&workers[i], i + 1);
                        }

                    } else if (WIFSIGNALED(status)) {

                        int signal_number = WTERMSIG(status);

                        char signal_message[150];

                        snprintf(signal_message, sizeof(signal_message),
                                 "Worker #%d terminated by signal: %d",
                                 i + 1, signal_number);

                        printf("[WARNING] %s\n", signal_message);

                        log_message("WARNING", signal_message);

                        restart_worker(&workers[i], i + 1);
                    }

                    break;
                }
            }
        }

        else if (pid == 0) {
            break;
        }

        else {

            if (errno == EINTR) {
                continue;
            }

            if (errno == ECHILD) {
                break;
            }

            int error_code = errno;

            perror("[ERROR] waitpid failed");

            char message[120];

            snprintf(message, sizeof(message),
                     "waitpid() failed | Error: %d",
                     error_code);

            log_message("ERROR", message);

            break;
        }
    }
}

void stop_workers(void) {

    shutting_down = 1;

    if (workers == NULL) {
        return;
    }

    printf("\n[INFO] Stopping all workers...\n");
    log_message("INFO", "Stopping all workers.");

    for (int i = 0; i < WORKER_COUNT; i++) {

        if (workers[i].pid <= 0) {
            continue;
        }

        char message[150];

        snprintf(message, sizeof(message),
                 "Sending SIGTERM to Worker #%d | PID: %d",
                 i + 1, workers[i].pid);

        log_message("INFO", message);

        if (kill(workers[i].pid, SIGTERM) == -1) {

            int error_code = errno;

            if (error_code != ESRCH) {

                char error_message[150];

                snprintf(error_message, sizeof(error_message),
                         "Failed to send SIGTERM to Worker #%d | Error: %d",
                         i + 1, error_code);

                log_message("ERROR", error_message);
            }
        }
    }

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

                char message[150];

                snprintf(message, sizeof(message),
                         "Worker #%d stopped gracefully | PID: %d",
                         i + 1, workers[i].pid);

                log_message("SUCCESS", message);

                workers[i].pid = -1;
                workers[i].status = WORKER_STOPPED;

            } else if (result == 0) {

                running_workers++;

            } else if (result == -1 && errno == ECHILD) {

                log_message("WARNING",
                            "Worker is no longer waitable.");

                workers[i].pid = -1;
                workers[i].status = WORKER_STOPPED;

            } else if (result == -1) {

                int error_code = errno;

                char message[150];

                snprintf(message, sizeof(message),
                         "waitpid() failed during graceful shutdown | Error: %d",
                         error_code);

                log_message("ERROR", message);

                running_workers++;
            }
        }

        if (running_workers == 0) {
            break;
        }

        sleep(1);
    }

    for (int i = 0; i < WORKER_COUNT; i++) {

        if (workers[i].pid <= 0) {
            continue;
        }

        pid_t worker_pid = workers[i].pid;

        char message[150];

        snprintf(message, sizeof(message),
                 "Worker #%d did not stop after SIGTERM. Sending SIGKILL | PID: %d",
                 i + 1, worker_pid);

        log_message("WARNING", message);

        int kill_result = kill(worker_pid, SIGKILL);
        int kill_error = (kill_result == -1) ? errno : 0;

        if (kill_result == -1 && kill_error != ESRCH) {

            char error_message[150];

            snprintf(error_message, sizeof(error_message),
                     "Failed to send SIGKILL to Worker #%d | Error: %d",
                     i + 1, kill_error);

            log_message("ERROR", error_message);
        }

        int status;
        pid_t result;

        if (kill_result == 0) {

            do {
                result = waitpid(worker_pid, &status, 0);
            } while (result == -1 && errno == EINTR);

        } else {

            do {
                result = waitpid(worker_pid, &status, WNOHANG);
            } while (result == -1 && errno == EINTR);
        }

        if (result == worker_pid) {

            char success_message[150];

            snprintf(success_message, sizeof(success_message),
                     "Worker #%d terminated and reaped | PID: %d",
                     i + 1, worker_pid);

            log_message("SUCCESS", success_message);

            workers[i].pid = -1;
            workers[i].status = WORKER_STOPPED;

        } else if (result == -1 && errno == ECHILD) {

            log_message("WARNING",
                        "Worker is no longer waitable.");

            workers[i].pid = -1;
            workers[i].status = WORKER_STOPPED;

        } else if (result == 0) {

            log_message("ERROR",
                        "Worker is still running. Could not confirm termination.");

        } else if (result == -1) {

            int error_code = errno;

            char error_message[150];

            snprintf(error_message, sizeof(error_message),
                     "waitpid() failed after SIGKILL | Error: %d",
                     error_code);

            log_message("ERROR", error_message);
        }
    }

    int unresolved_workers = 0;

    for (int i = 0; i < WORKER_COUNT; i++) {

        if (workers[i].pid > 0) {
            unresolved_workers++;
        }
    }

    if (unresolved_workers == 0) {

        free(workers);
        workers = NULL;

        printf("[SUCCESS] All workers have been stopped.\n");
        log_message("SUCCESS", "All workers have been stopped.");

    } else {

        printf("[WARNING] Some workers could not be confirmed stopped.\n");
        log_message("WARNING",
                    "Some workers could not be confirmed stopped. Worker data retained.");
    }
}

void print_worker_status(void) {

    if (workers == NULL) {

        printf("[WARNING] No workers have been initialized.\n");

        log_message("WARNING",
                    "Worker status requested, but no workers have been initialized.");

        return;
    }

    printf("\n");
    printf("==================================================\n");
    printf("                  WORKER STATUS                   \n");
    printf("==================================================\n\n");

    log_message("INFO", "Worker status requested.");

    for (int i = 0; i < WORKER_COUNT; i++) {

        const char *status;

        switch (workers[i].status) {

            case WORKER_RUNNING:
                status = "RUNNING";
                break;

            case WORKER_STOPPED:
                status = "STOPPED";
                break;

            case WORKER_RESTARTING:
                status = "RESTARTING";
                break;

            case WORKER_FAILED:
                status = "FAILED";
                break;

            default:
                status = "UNKNOWN";
                break;
        }

        printf("Worker #%d | PID: %d | STATUS: %s\n",
               i + 1,
               workers[i].pid,
               status);

        printf("           Restart Count: %d/%d\n",
               workers[i].restart_count,
               MAX_RESTARTS);

        char message[200];

        snprintf(message, sizeof(message),
                 "Worker #%d | PID: %d | STATUS: %s | Restart Count: %d/%d",
                 i + 1,
                 workers[i].pid,
                 status,
                 workers[i].restart_count,
                 MAX_RESTARTS);

        log_message("INFO", message);
    }

    printf("\n==================================================\n\n");
}



