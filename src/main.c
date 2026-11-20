#include "process_manager.h"
#include "signal_manager.h"

#include <stdio.h>
#include <unistd.h>

int main(void) {

    /* Register signal handlers */
    setup_signal_handlers();

    printf("Watchdog process manager started.\n");

    /* Start child processes */
    start_workers();

    /* Main monitoring loop */
    while (!shutdown_requested) {

        if (child_exited) {
            child_exited = 0;
            check_children();
        }

        sleep(1);
    }

    /* Graceful shutdown */
    printf("\nShutdown signal received.\n");

    stop_workers();

    printf("Watchdog stopped.\n");

    return 0;
}
