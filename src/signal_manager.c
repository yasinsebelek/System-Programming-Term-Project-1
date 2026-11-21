#include "signal_manager.h"

#include <stdio.h>

volatile sig_atomic_t child_exited = 0;
volatile sig_atomic_t shutdown_requested = 0;
volatile sig_atomic_t status_requested = 0;
volatile sig_atomic_t periodic_check_requested = 0;

static void handle_sigchld(int signal_number) {
    (void)signal_number;
    child_exited = 1;
}

static void handle_sigint(int signal_number) {
    (void)signal_number;
    shutdown_requested = 1;
}

static void handle_sigterm(int signal_number) {
    (void)signal_number;
    shutdown_requested = 1;
}

static void handle_sigusr1(int signal_number) {
    (void)signal_number;
    status_requested = 1;
}

static void handle_sigalrm(int signal_number) {
    (void)signal_number;
    periodic_check_requested = 1;
}

int setup_signal_handlers(void) {

    struct sigaction signal_action = {0};

    sigemptyset(&signal_action.sa_mask);
    signal_action.sa_flags = SA_RESTART;

    signal_action.sa_handler = handle_sigchld;
    if (sigaction(SIGCHLD, &signal_action, NULL) == -1) {
        perror("sigaction SIGCHLD");
        return -1;
    }

    signal_action.sa_handler = handle_sigint;
    if (sigaction(SIGINT, &signal_action, NULL) == -1) {
        perror("sigaction SIGINT");
        return -1;
    }

    signal_action.sa_handler = handle_sigterm;
    if (sigaction(SIGTERM, &signal_action, NULL) == -1) {
        perror("sigaction SIGTERM");
        return -1;
    }

    signal_action.sa_handler = handle_sigusr1;
    if (sigaction(SIGUSR1, &signal_action, NULL) == -1) {
        perror("sigaction SIGUSR1");
        return -1;
    }

    signal_action.sa_handler = handle_sigalrm;
    if (sigaction(SIGALRM, &signal_action, NULL) == -1) {
        perror("sigaction SIGALRM");
        return -1;
    }

    return 0;
}