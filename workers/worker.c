#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main(void) {

pid_t pid = getpid();
pid_t ppid = getppid();

printf("\n[WORKER STARTED] PID: %d | PARENT: %d\n", pid, ppid);
fflush(stdout);

while (1) {

    sleep(3);

    printf("[WORKER %d] STATUS: RUNNING\n", pid);
    fflush(stdout);
}

return EXIT_SUCCESS;

}
