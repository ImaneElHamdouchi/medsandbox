#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int main() {
    printf("Fork bomb test started...\n");

    fflush(stdout);

    while (1) {

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork blocked");

            sleep(2);

            return 1;
        }

        if (pid == 0) {
            sleep(10);
            return 0;
        }
    }

    return 0;
}
