#include "signal_manager.h"

#include <stdio.h>
#include <stdlib.h>


/* Global signal flags */
volatile sig_atomic_t child_exited = 0;
volatile sig_atomic_t shutdown_requested = 0;
volatile sig_atomic_t status_requested = 0;


/* SIGCHLD handler */
static void handle_sigchld(int signal_number) {
    (void)signal_number;

    child_exited = 1;
}


/* SIGINT handler */
static void handle_sigint(int signal_number) {
    (void)signal_number;

    shutdown_requested = 1;
}


/* SIGTERM handler */
static void handle_sigterm(int signal_number) {
    (void)signal_number;

    shutdown_requested = 1;
}


/* Register signal handlers */
void setup_signal_handlers(void) {

    struct sigaction signal_action = {0};

    sigemptyset(&signal_action.sa_mask);

    signal_action.sa_flags = SA_RESTART;


    /* Register SIGCHLD */
    signal_action.sa_handler = handle_sigchld;

    if (sigaction(SIGCHLD, &signal_action, NULL) == -1) {
        perror("sigaction SIGCHLD");
        exit(EXIT_FAILURE);
    }
    signal_action.sa_handler = handle_sigint;

    if (sigaction(SIGINT, &signal_action, NULL) == -1) {
        perror("sigaction SIGINT");
        exit(EXIT_FAILURE);
    }
    signal_action.sa_handler = handle_sigterm;

    if (sigaction(SIGTERM, &signal_action, NULL) == -1) {
        perror("sigaction SIGTERM");
        exit(EXIT_FAILURE);
    }
}
