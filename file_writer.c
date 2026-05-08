#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("File writer started...\n");

    fflush(stdout);

    FILE *file = fopen("big_output.txt", "w");

    if (!file) {
        perror("fopen");
        return 1;
    }

    for (int i = 0; i < 10000000; i++) {
        fprintf(file, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\n");
    }

    fclose(file);

    return 0;
}
