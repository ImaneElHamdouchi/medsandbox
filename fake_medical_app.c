#include <stdio.h>
#include <unistd.h>

int main() {
    printf("Fake medical app running...\n");

    fflush(stdout);

    sleep(10);

    printf("Fake medical app finished.\n");

    return 0;
}
