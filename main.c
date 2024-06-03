#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_CONNECTIONS 100

volatile sig_atomic_t running = 1;

void sigint_handler(int signo) {
    if (signo == SIGINT) {
        running = 0;
    }
}

int home_get(int client_socket) {
    char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                      "<html><body><h1>Hello world!</h1></body></html>";

    printf("It works!!!\n");

    if (send(client_socket, response, strlen(response), 0) == -1) {
        printf("Failed send buffer\n");
        close(client_socket);
        return -1;
    }

    close(client_socket);
    return 0;
}

int main() {
    signal(SIGINT, sigint_handler);

    uint16_t port = 8080;

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        printf("Failed to create server socket\n");
        return -1;
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        printf("Failed to set port address for immediately re-use after the socket is closed\n");
        close(server_socket);
        return -1;
    }

    // Configure server address
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,        // IPv4
        .sin_port = htons(port),      // Convert the port number from host byte order to network byte order (big-endian)
        .sin_addr.s_addr = INADDR_ANY // Listen on all available network interfaces (IPv4 addresses)
    };

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        printf("Failed to bind socket to address and port\n");
        close(server_socket);
        return -1;
    }

    if (listen(server_socket, MAX_CONNECTIONS) == -1) {
        printf("Failed to set server socket to listen for incoming connections\n");
        close(server_socket);
        return -1;
    }

    printf("Server listening on port: %d...\n", port);

    while (running) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);

        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);

        if (!running) {
            break;
        }

        if (client_socket == -1) {
            printf("Failed extract connection request\n");
            close(server_socket);
            return -1;
        }

        if (home_get(client_socket) == -1) {
            printf("Failed to process request\n");
            close(server_socket);
            return -1;
        }
    }
}