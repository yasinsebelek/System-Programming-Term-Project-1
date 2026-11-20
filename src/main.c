#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#define WORKER_COUNT 2

volatile sig_atomic_t child_changed = 0;

// SIGCHLD handler
void handle_sigchld(int sig) {
(void)sig; 
 child_changed = 1;
}

// Create a worker process
pid_t start_worker(int worker_number) {

    pid_t pid = fork();

    if (pid < 0) {
        perror("fork failed");
        return -1;
    }

    if (pid == 0) {

        printf("Worker %d is starting...\n", worker_number);
        fflush(stdout);

        execl("./bin/worker", "worker", (char *)NULL);

        perror("exec failed");
        exit(EXIT_FAILURE);
    }

    printf("Worker %d created. PID: %d\n", worker_number, pid);

    return pid;
}

int main() {

    printf("================================\n");
    printf("        WATCHDOG STARTED        \n");
    printf("================================\n");

    // Configure SIGCHLD handler
    struct sigaction sa;

    sa.sa_handler = handle_sigchld;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP;

    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction failed");
        return EXIT_FAILURE;
    }

    // Store worker PIDs
    pid_t workers[WORKER_COUNT];

    // Start workers
    for (int i = 0; i < WORKER_COUNT; i++) {
        workers[i] = start_worker(i + 1);
    }

    printf("\nWatchdog PID: %d\n", getpid());
    printf("All workers have been started.\n\n");

    // Main monitoring loop
    while (1) {

        if (child_changed) {

            child_changed = 0;

            int status;
            pid_t terminated_pid;

            while ((terminated_pid = waitpid(-1, &status, WNOHANG)) > 0) {

                printf("\nWorker terminated. PID: %d\n", terminated_pid);

                if (WIFEXITED(status)) {
                    printf("Exit status: %d\n", WEXITSTATUS(status));
                }
                else if (WIFSIGNALED(status)) {
                    printf("Terminated by signal: %d\n", WTERMSIG(status));
                }
            }
        }

        sleep(1);
    }

    return 0;
}
