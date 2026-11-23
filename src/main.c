#include "process_manager.h"
#include "signal_manager.h"
#include "logger.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <time.h>

#define PERIODIC_CHECK_INTERVAL 5

int main(void) {

    if (init_logger() == -1) {
        return EXIT_FAILURE;
    }

    printf("\n");
    printf("==================================================\n");
    printf("             WATCHDOG PROCESS MANAGER             \n");
    printf("==================================================\n\n");

    if (setup_signal_handlers() == -1) {
        fprintf(stderr, "[ERROR] Failed to install signal handlers.\n");
        log_message("ERROR", "Failed to install signal handlers.");
        close_logger();
        return EXIT_FAILURE;
    }

    pid_t watchdog_pid = getpid();
    char startup_message[100];

    snprintf(startup_message, sizeof(startup_message),
             "Watchdog started | PID: %d", (int)watchdog_pid);

    printf("[INFO] %s\n", startup_message);
    printf("[INFO] Initializing workers...\n\n");

    log_message("INFO", startup_message);
    log_message("INFO", "Initializing workers...");

    if (start_workers() == -1) {

        fprintf(stderr, "\n[ERROR] Watchdog startup failed.\n");
        fprintf(stderr, "[ERROR] Application terminated.\n");

        log_message("ERROR", "Watchdog startup failed.");

        close_logger();

        return EXIT_FAILURE;
    }

    int timer_started = 0;

    if (!shutdown_requested) {
        if (start_periodic_timer(PERIODIC_CHECK_INTERVAL) == -1) {
            stop_workers();
            close_logger();
            return EXIT_FAILURE;
        }
        timer_started = 1;

        printf("\n");
        printf("==================================================\n");
        printf("              WATCHDOG IS RUNNING                 \n");
        printf("==================================================\n\n");
    }

    while (!shutdown_requested) {

        if (child_exited) {
            child_exited = 0;
            check_children();
        }

        if (periodic_check_requested) {
            periodic_check_requested = 0;
            periodic_check_workers();
        }

        if (status_requested) {
            status_requested = 0;
            print_worker_status();
        }

        struct timespec ts = {1, 0};
        nanosleep(&ts, NULL);
    }

    if (timer_started) {
        stop_periodic_timer();
    }

    printf("\n");
    printf("==================================================\n");
    printf("                 SHUTTING DOWN                    \n");
    printf("==================================================\n\n");

    printf("[INFO] Shutdown signal received.\n");

    log_message("INFO", "Watchdog shutdown started.");

    stop_workers();

    log_message("SUCCESS", "Watchdog shutdown completed.");

    printf("\n");
    printf("==================================================\n");
    printf("                WATCHDOG STOPPED                  \n");
    printf("==================================================\n\n");

    close_logger();

    return EXIT_SUCCESS;
}
