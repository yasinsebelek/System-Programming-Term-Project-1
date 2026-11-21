#include "process_manager.h"
#include "signal_manager.h"
#include "logger.h"

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>

int main(void) {

    if (init_logger() == -1) {
        return EXIT_FAILURE;
    }

    printf("\n");
    printf("==================================================\n");
    printf("             WATCHDOG PROCESS MANAGER             \n");
    printf("==================================================\n\n");

    setup_signal_handlers();

    printf("[INFO] Watchdog started.\n");
    printf("[INFO] Initializing workers...\n\n");

    log_message("INFO", "Watchdog started.");
    log_message("INFO", "Initializing workers...");

    if (start_workers() == -1) {

        fprintf(stderr, "\n[ERROR] Watchdog startup failed.\n");
        fprintf(stderr, "[ERROR] Application terminated.\n");

        log_message("ERROR", "Watchdog startup failed.");

        close_logger();

        return EXIT_FAILURE;
    }

    printf("\n[INFO] All workers initialized successfully.\n");

    
    struct itimerval timer = {0};

    timer.it_value.tv_sec = 5;
    timer.it_interval.tv_sec = 5;

    if (setitimer(ITIMER_REAL, &timer, NULL) == -1) {

        perror("setitimer");

        log_message("ERROR", "Failed to start periodic timer.");

        stop_workers();
        close_logger();

        return EXIT_FAILURE;
    }

    log_message("INFO", "Periodic child monitoring started.");

    printf("\n");
    printf("==================================================\n");
    printf("              WATCHDOG IS RUNNING                 \n");
    printf("==================================================\n\n");

    while (!shutdown_requested) {

        if (child_exited) {
            child_exited = 0;
            check_children();
        }

        if (periodic_check_requested) {
            periodic_check_requested = 0;
            check_children();
        }

        if (status_requested) {
            status_requested = 0;
            print_worker_status();
        }

        sleep(1);
    }

    

    struct itimerval stop_timer = {0};

    if (setitimer(ITIMER_REAL, &stop_timer, NULL) == -1) {
        perror("setitimer stop");
        log_message("ERROR", "Failed to stop periodic timer.");
    }

    log_message("INFO", "Periodic child monitoring stopped.");

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