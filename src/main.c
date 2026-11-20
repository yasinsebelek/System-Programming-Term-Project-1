#include "process_manager.h"

#include <stdio.h>
#include <unistd.h>
#include <signal.h>

int main(void) {

    printf("Watchdog process manager started.\n");

    // Start worker processes
    start_workers();

    // Monitor worker processes
    while (1) {

        check_children();

        sleep(1);
    }

    // Stop all workers
    stop_workers();

    printf("Watchdog stopped.\n");

    return 0;
}
