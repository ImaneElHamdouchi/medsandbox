#include <stdio.h>
#include <sys/ptrace.h>

int main() {
    printf("Ptrace test started...\n");

    fflush(stdout);

    long result =
        ptrace(PTRACE_TRACEME, 0, NULL, NULL);

    if (result == -1) {
        perror("ptrace");
        return 1;
    }

    printf("ptrace succeeded.\n");

    return 0;
}
