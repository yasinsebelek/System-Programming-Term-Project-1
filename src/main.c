#include "process_manager.h"
#include "signal_manager.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {

printf("\n");
printf("==================================================\n");
printf("             WATCHDOG PROCESS MANAGER             \n");
printf("==================================================\n\n");

setup_signal_handlers();

printf("[INFO] Watchdog started.\n");
printf("[INFO] Initializing workers...\n\n");

if (start_workers() == -1) {

    fprintf(stderr, "\n[ERROR] Watchdog startup failed.\n");
    fprintf(stderr, "[ERROR] Application terminated.\n");

    return EXIT_FAILURE;
}

printf("\n[INFO] All workers initialized successfully.\n");

printf("\n");
printf("==================================================\n");
printf("              WATCHDOG IS RUNNING                 \n");
printf("==================================================\n\n");

while (!shutdown_requested) {

    if (child_exited) {
        child_exited = 0;
        check_children();
    }

    sleep(1);
}

printf("\n");
printf("==================================================\n");
printf("                 SHUTTING DOWN                    \n");
printf("==================================================\n\n");

printf("[INFO] Shutdown signal received.\n");

stop_workers();

printf("\n");
printf("==================================================\n");
printf("                WATCHDOG STOPPED                  \n");
printf("==================================================\n\n");

return EXIT_SUCCESS;

}
