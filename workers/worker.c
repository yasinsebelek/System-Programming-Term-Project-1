#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <time.h>

static volatile sig_atomic_t terminate_requested = 0;
static volatile sig_atomic_t normal_exit_requested = 0;

static void handle_sigterm(int sig) {
    (void)sig;
    terminate_requested = 1;
}

static void handle_sigusr2(int sig) {
    (void)sig;
    normal_exit_requested = 1;
}

int main(void) {

    pid_t pid = getpid();
    pid_t ppid = getppid();
    const char *mode = getenv("WORKER_MODE");

    struct sigaction sa = {0};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    /* Ignore SIGINT so watchdog manages Ctrl+C shutdown via SIGTERM */
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        perror("[WORKER] sigaction SIGINT");
        return EXIT_FAILURE;
    }

    if (mode != NULL && strcmp(mode, "ignore_sigterm") == 0) {
        sa.sa_handler = SIG_IGN;
    } else {
        sa.sa_handler = handle_sigterm;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("[WORKER] sigaction SIGTERM");
        return EXIT_FAILURE;
    }

    sa.sa_handler = handle_sigusr2;
    if (sigaction(SIGUSR2, &sa, NULL) == -1) {
        perror("[WORKER] sigaction SIGUSR2");
        return EXIT_FAILURE;
    }

    printf("\n[WORKER STARTED] PID: %d | PARENT: %d\n", pid, ppid);
    fflush(stdout);

    if (mode != NULL && strcmp(mode, "exit_ok") == 0) {
        struct timespec ts = {2, 0};
        nanosleep(&ts, NULL);
        printf("[WORKER %d] Completed normally (exit 0).\n", pid);
        fflush(stdout);
        return EXIT_SUCCESS;
    }

    if (mode != NULL && strcmp(mode, "exit_err") == 0) {
        struct timespec ts = {2, 0};
        nanosleep(&ts, NULL);
        printf("[WORKER %d] Encountered error (exit 1).\n", pid);
        fflush(stdout);
        return EXIT_FAILURE;
    }

    while (!terminate_requested && !normal_exit_requested) {
        struct timespec ts = {3, 0};
        nanosleep(&ts, NULL);

        if (terminate_requested || normal_exit_requested) {
            break;
        }

        printf("[WORKER %d] STATUS: RUNNING\n", pid);
        fflush(stdout);
    }

    if (terminate_requested) {
        printf("[WORKER %d] Received SIGTERM, exiting gracefully.\n", pid);
    } else {
        printf("[WORKER %d] Completed work normally (exit 0).\n", pid);
    }
    fflush(stdout);

    return EXIT_SUCCESS;
}