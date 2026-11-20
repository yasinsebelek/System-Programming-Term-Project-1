#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main() {

    pid_t pid = getpid();
    pid_t ppid = getppid();

    printf("================================\n");
    printf("       Worker Process Started   \n");
    printf("================================\n");

    printf("Worker PID: %d\n", pid);
    printf("Parent PID: %d\n", ppid);

    while (1) {
        sleep(3);

        printf("Worker (%d) is running...\n", pid);
	//çıktıyı hemen gönderir
        fflush(stdout);

    }

    return 0;
}
