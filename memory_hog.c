#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main() {
    printf("Memory hog started...\n");

    fflush(stdout);

    size_t size = 1024UL * 1024UL * 1024UL;

    void *ptr = malloc(size);

    if (!ptr) {
        perror("malloc failed");
        return 1;
    }

    sleep(5);

    free(ptr);

    return 0;
}
