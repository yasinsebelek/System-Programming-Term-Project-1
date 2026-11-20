#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(void) {

    pid_t pid = getpid();
    pid_t ppid = getppid();

    printf("\n");
    printf("+--------------------------------------+\n");
    printf("|          WORKER PROCESS              |\n");
    printf("+--------------------------------------+\n");
    printf("| Status     : STARTED                 |\n");
    printf("| Worker PID : %-22d |\n", pid);
    printf("| Parent PID : %-22d |\n", ppid);
    printf("+--------------------------------------+\n");

    fflush(stdout);

    while (1) {

        sleep(3);

        printf("[WORKER %d] Status: RUNNING\n", pid);

        fflush(stdout);
    }

    return EXIT_SUCCESS;
}
