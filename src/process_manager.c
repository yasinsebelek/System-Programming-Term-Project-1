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

        perror("fork failed");
        return -1;

    } else if (pid == 0) {


        execl("./bin/worker", "worker", (char *)NULL);

        perror("exec failed");
        _exit(EXIT_FAILURE);

    } else {

        printf("Worker created. PID: %d\n", pid);

    }

    return pid;
}


void start_workers(void) {

    if (workers != NULL) {
        return;
    }

    workers = malloc(sizeof(Worker) * WORKER_COUNT);

    if (workers == NULL) {
        perror("malloc failed");
        return;
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

        } else {

            workers[i].status = WORKER_FAILED;
        }
    }

    printf("All workers have been started.\n");
}


void restart_worker(Worker *worker) {

    if (worker->restart_count >= MAX_RESTARTS) {

        printf("Worker reached maximum restart limit.\n");

        worker->status = WORKER_FAILED;
        worker->pid = -1;

        return;
    }
// her denemede artırıyoruz ki sonsuz bir döngüye girmesin başarılı sonuçta count  artırsaydık başarılı olna kadar denerdi 
    worker->status = WORKER_RESTARTING;

    worker->restart_count++;

    printf("Restarting worker... Attempt: %d/%d\n",
           worker->restart_count, MAX_RESTARTS);

    worker->pid = create_worker();

    if (worker->pid > 0) {

        worker->status = WORKER_RUNNING;

        printf("Worker restarted successfully. PID: %d\n",
               worker->pid);

    } else {

        worker->status = WORKER_FAILED;

        printf("Worker restart failed.\n");
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

                    printf("Worker %d (PID: %d) terminated.\n", i, pid);

                    workers[i].pid = -1;

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

                    }

                    else if (WIFSIGNALED(status)) {

                        int signal_number = WTERMSIG(status);

                        printf("Worker terminated by signal: %d\n",
                               signal_number);

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

            perror("waitpid failed");
            break;
        }
    }
}


void stop_workers(void) {

    shutting_down = 1;

    if (workers == NULL) {
        return;
    }

    printf("Stopping all workers...\n");

    for (int i = 0; i < WORKER_COUNT; i++) {

        if (workers[i].pid > 0) {

            printf("Sending SIGTERM to worker PID: %d\n",
                   workers[i].pid);

            if (kill(workers[i].pid, SIGTERM) == -1 && errno != ESRCH) {
                perror("kill SIGTERM failed");
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

        if (running_workers == 0) {
            break;
        }

        sleep(1);
    }

    for (int i = 0; i < WORKER_COUNT; i++) {

        if (workers[i].pid > 0) {

            printf("Worker PID %d did not stop. Sending SIGKILL.\n",
                   workers[i].pid);

            if (kill(workers[i].pid, SIGKILL) == -1 && errno != ESRCH) {
                perror("kill SIGKILL failed");
            }

            int status;
            pid_t result;

            do {
                result = waitpid(workers[i].pid, &status, 0);
            } while (result == -1 && errno == EINTR);

            workers[i].pid = -1;
            workers[i].status = WORKER_STOPPED;
        }
    }

    free(workers);
    workers = NULL;

    printf("All workers have been stopped.\n");
}
