#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int main() {
    printf("Network test started...\n");

    fflush(stdout);

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    if (sock < 0) {
        perror("socket");
        return 1;
    }

    printf("Socket created successfully.\n");

    close(sock);

    return 0;
}
