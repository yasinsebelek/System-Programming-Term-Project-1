#include "process_manager.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>

#define SHUTDOWN_TIMEOUT 5

static Worker *workers = NULL;

static int shutting_down = 0;


pid_t create_worker(void) {

fflush(NULL);

pid_t pid = fork();

if (pid < 0) {

    perror("[ERROR] fork failed");
    return -1;

} else if (pid == 0) {

    execl("./bin/worker", "worker", (char *)NULL);

    perror("[ERROR] exec failed");
    _exit(EXIT_FAILURE);

} else {

    printf("[INFO] Child process created | PID: %d\n", pid);

}

return pid;


}

int start_workers(void) {

if (workers != NULL) {
    return 0;
}

workers = malloc(sizeof(Worker) * WORKER_COUNT);

if (workers == NULL) {
    perror("[ERROR] malloc failed");
    return -1;
}

/* Initialize worker information */
for (int i = 0; i < WORKER_COUNT; i++) {

    workers[i].pid = -1;
    workers[i].restart_count = 0;
    workers[i].status = WORKER_STOPPED;
}

/* Create workers one by one */
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

        stop_workers();

        return -1;
    }
}

printf("\n[SUCCESS] All workers initialized successfully.\n");

return 0;



}


void restart_worker(Worker *worker) {


if (worker->restart_count >= MAX_RESTARTS) {

    printf("[ERROR] Worker reached maximum restart limit (%d).\n",
           MAX_RESTARTS);

    worker->status = WORKER_FAILED;
    worker->pid = -1;

    return;
}

worker->status = WORKER_RESTARTING;

worker->restart_count++;

printf("[INFO] Restarting worker | Attempt: %d/%d\n",
       worker->restart_count, MAX_RESTARTS);

worker->pid = create_worker();

if (worker->pid > 0) {

    worker->status = WORKER_RUNNING;

    printf("[SUCCESS] Worker restarted successfully | PID: %d\n",
           worker->pid);

} else {

    worker->status = WORKER_FAILED;

    printf("[ERROR] Worker restart failed.\n");
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

                workers[i].pid = -1;

                if (WIFEXITED(status)) {

                    int exit_code = WEXITSTATUS(status);

                    if (exit_code == 0) {

                        printf("[INFO] Worker #%d exited normally.\n",
                               i + 1);

                        workers[i].status = WORKER_STOPPED;

                    } else {

                        printf("[ERROR] Worker #%d exited with error | Code: %d\n",
                               i + 1, exit_code);

                        restart_worker(&workers[i]);
                    }

                } else if (WIFSIGNALED(status)) {

                    int signal_number = WTERMSIG(status);

                    printf("[WARNING] Worker #%d terminated by signal: %d\n",
                           i + 1, signal_number);

                    restart_worker(&workers[i]);
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

        perror("[ERROR] waitpid failed");
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

/* Send SIGTERM to all running workers */
for (int i = 0; i < WORKER_COUNT; i++) {

    if (workers[i].pid > 0) {

        printf("[INFO] Sending SIGTERM to Worker #%d | PID: %d\n",
               i + 1, workers[i].pid);

        if (kill(workers[i].pid, SIGTERM) == -1 && errno != ESRCH) {
            perror("[ERROR] kill SIGTERM failed");
        }
    }
}

/* Wait for graceful shutdown */
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

            printf("[SUCCESS] Worker #%d stopped gracefully | PID: %d\n",
                   i + 1, workers[i].pid);

            workers[i].pid = -1;
            workers[i].status = WORKER_STOPPED;

        } else if (result == 0) {

            running_workers++;

        } else if (result == -1 && errno == ECHILD) {

            workers[i].pid = -1;
            workers[i].status = WORKER_STOPPED;

        } else if (result == -1) {

            perror("[ERROR] waitpid failed");
        }
    }

    if (running_workers == 0) {
        break;
    }

    sleep(1);
}

/* Forcefully terminate workers that did not stop */
for (int i = 0; i < WORKER_COUNT; i++) {

    if (workers[i].pid > 0) {

        printf("[WARNING] Worker #%d did not stop. Sending SIGKILL | PID: %d\n",
               i + 1, workers[i].pid);

        if (kill(workers[i].pid, SIGKILL) == -1 && errno != ESRCH) {
            perror("[ERROR] kill SIGKILL failed");
        }

        int status;
        pid_t result;

        do {
            result = waitpid(workers[i].pid, &status, 0);
        } while (result == -1 && errno == EINTR);

        if (result == workers[i].pid ||
            (result == -1 && errno == ECHILD)) {

            workers[i].pid = -1;
            workers[i].status = WORKER_STOPPED;

        } else if (result == -1) {

            perror("[ERROR] waitpid after SIGKILL failed");
        }
    }
}

free(workers);
workers = NULL;

printf("[SUCCESS] All workers have been stopped.\n");




}
