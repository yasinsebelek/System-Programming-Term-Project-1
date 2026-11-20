#include "process_manager.h"

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

static volatile sig_atomic_t shutdown_requested = 0;

void handle_signal(int signal_number) {
    shutdown_requested = 1;
}

int main(void) {

    struct sigaction sa = {0};

    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    printf("Watchdog process manager started.\n");

    start_workers();

    while (!shutdown_requested) {

        check_children();

        sleep(1);
    }

    printf("\nShutdown signal received.\n");

    stop_workers();

    printf("Watchdog stopped.\n");

    return 0;
}
