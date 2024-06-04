#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void sigint_handler(int signo);
int router(void *p_client_socket, uint8_t thread_index);
void *thread_function(void *arg);
void enqueue_client_socket(int *client_socket);
void *dequeue_client_socket();
int home_get(int client_socket, uint8_t thread_index);

typedef struct ClientSocketQueueNode {
    struct ClientSocketQueueNode *next;
    int *client_socket;
} ClientSocketQueueNode;

#define MAX_CONNECTIONS 100
#define POOL_SIZE 3

volatile sig_atomic_t keep_running = 1;

pthread_t thread_pool[POOL_SIZE];
pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t thread_condition_var = PTHREAD_COND_INITIALIZER;

ClientSocketQueueNode *head_client_socket_queue = NULL;
ClientSocketQueueNode *tail_client_socket_queue = NULL;

int main() {
    int retval = 0;

    // Registers a signal handler for SIGINT (to terminate the process)
    // to exit the program gracefully for Valgrind to show the program report.
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        fprintf(stderr, "Failed to set up signal handler for SIGINT\nError code: %d\n", errno);
        retval = -1;
        goto main_exit;
    }

    uint16_t port = 8080;

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        printf("Failed to create server socket\n");
        retval = -1;
        goto main_exit;
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        printf("Failed to set port address for immediately re-use after the socket is closed\n");
        retval = -1;
        goto main_socket_cleanup;
    }

    // Configure server address
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,        // IPv4
        .sin_port = htons(port),      // Convert the port number from host byte order to network byte order (big-endian)
        .sin_addr.s_addr = INADDR_ANY // Listen on all available network interfaces (IPv4 addresses)
    };

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        printf("Failed to bind socket to address and port\n");
        retval = -1;
        goto main_socket_cleanup;
    }

    if (listen(server_socket, MAX_CONNECTIONS) == -1) {
        printf("Failed to set server socket to listen for incoming connections\n");
        retval = -1;
        goto main_socket_cleanup;
    }

    printf("Server listening on port: %d...\n", port);

    // Create thread pool
    for (uint8_t i = 0; i < POOL_SIZE; i++) {
        // Thread might take some time time to create, but 'i' will keep mutating as the program runs,
        // storing the value of 'i' in the heap at the time of iterating ensures that thread_function
        // receives the correct value even when 'i' has moved on.
        unsigned short *p_iteration = malloc(sizeof(unsigned short));
        *p_iteration = i;
        if (pthread_create(&thread_pool[*p_iteration], NULL, &thread_function, p_iteration) != 0) {
            fprintf(stderr, "Failed to create thread at iteration n° %d\nError code: %d\n", *p_iteration, errno);
            retval = -1;
            goto main_threads_cleanup;
        }
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof client_addr;

        // The while loop will wait at accept for a new client to connect
        int client_socket;
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);

        // When a signal to exit the program is received, 'accept' will error with -1. To verify that
        // client_socket being -1 isn't because of program termination, check whether the program
        // has received a signal to exit.
        if (keep_running == 0) {
            // Send signal to threads to resume execusion so they can proceed to cleanup
            pthread_cond_signal(&thread_condition_var);
            retval = 0;
            goto main_threads_cleanup;
        }

        // At this point, we know that client_socket being -1 wouldn't have been caused by program termination
        if (client_socket == -1) {
            fprintf(stderr, "Failed to create client socket\nError code: %d\n", errno);
            retval = -1;
            goto main_threads_cleanup;
        }

        // Store fd number for client socket in the heap so it can be pointed by a queue data structure
        // and used when it is needed.
        int *p_client_socket = malloc(sizeof(int));
        *p_client_socket = client_socket;

        if (pthread_mutex_lock(&thread_mutex) != 0) {
            fprintf(stderr, "Failed to lock pthread mutex\nError code: %d\n", errno);
            retval = -1;
            goto main_threads_cleanup;
        }

        printf("- New request: enqueue_client_socket client socket fd %d\n", *p_client_socket);
        enqueue_client_socket(p_client_socket);

        if (pthread_cond_signal(&thread_condition_var) != 0) {
            fprintf(stderr, "Failed to send pthread signal\nError code: %d\n", errno);
            retval = -1;
            goto main_threads_cleanup;
        }

        if (pthread_mutex_unlock(&thread_mutex) != 0) {
            fprintf(stderr, "Failed to unlock pthread mutex\nError code: %d\n", errno);
            retval = -1;
            goto main_threads_cleanup;
        }
    }

main_threads_cleanup:
    for (uint8_t i = 0; i < POOL_SIZE; i++) {
        // At this point, the variable 'keep_running' is 0.
        // Signal all threads to resume execution to perform cleanup.
        pthread_cond_signal(&thread_condition_var);
    }

    for (uint8_t i = 0; i < POOL_SIZE; i++) {
        printf("(Thread %d) Cleaning up thread %lu\n", i, thread_pool[i]);

        if (pthread_join(thread_pool[i], NULL) != 0) {
            fprintf(stderr, "Failed to join thread at position %d in the thread pool\nError code: %d\n", i, errno);
        }
    }

main_socket_cleanup:
    close(server_socket);

main_exit:
    return retval;
}

void *thread_function(void *arg) {
    uint8_t *p_thread_index = (uint8_t *)arg;
    pthread_t tid = pthread_self();
    printf("(Thread %d) Setting up thread %lu\n", *p_thread_index, tid);

    while (1) {
        int *p_client_socket;

        if (pthread_mutex_lock(&thread_mutex) != 0) {
            /** TODO: cleanup */
        }

        // Check queue for client request
        if ((p_client_socket = dequeue_client_socket()) == NULL) {
            // At this point, we know the queue was empty, so hold thread execusion
            pthread_cond_wait(&thread_condition_var, &thread_mutex); // On hold, waiting to receive a signal...
            // Signal to proceed with execusion has been sent

            if (keep_running == 0) {
                pthread_mutex_unlock(&thread_mutex);
                goto out;
            }

            p_client_socket = dequeue_client_socket();
            printf("(Thread %d) Dequeueing(received signal)...\n", *p_thread_index);
            goto skip_print;
        }

        printf("(Thread %d) Dequeueing...\n", *p_thread_index);

    skip_print:

        if (pthread_mutex_unlock(&thread_mutex) != 0) {
            /** TODO: cleanup */
        }

        if (p_client_socket != NULL) {
            if (router(p_client_socket, *p_thread_index) == -1) {
                printf("router returned error!\n");
                // NOTE: maybe uncomment 'break' to exit thread silently on router error? (Consider whether there is a better way to handle this scenario)
                // break;
            }
        }
    }

out:
    printf("(Thread %d) Out of while loop\n", *p_thread_index);

    free(p_thread_index);
    p_thread_index = NULL;

    return NULL;
}

int router(void *p_client_socket, uint8_t thread_index) {
    int retval = 0;

    // At this point we don't need to hold the client_socket in heap anymore, we can work
    // with it in the stack from now on
    int client_socket = *((int *)p_client_socket);

    free(p_client_socket);
    p_client_socket = NULL;

    if (home_get(client_socket, thread_index) == -1) {
        printf("Failed to process request\n");
        retval = -1;
        goto cleanup;
    }

cleanup:
    return retval;
}

int home_get(int client_socket, uint8_t thread_index) {
    char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                      "<html><body><h1>Hello world!</h1></body></html>";

    printf("It works!!! (on thread %d)\n", thread_index);

    if (send(client_socket, response, strlen(response), 0) == -1) {
        printf("Failed send buffer\n");
        close(client_socket);
        return -1;
    }

    close(client_socket);
    return 0;
}

void enqueue_client_socket(int *client_socket) {
    ClientSocketQueueNode *new_node = malloc(sizeof(ClientSocketQueueNode));
    new_node->client_socket = client_socket;
    new_node->next = NULL;

    if (tail_client_socket_queue == NULL) {
        head_client_socket_queue = new_node;
    } else {
        tail_client_socket_queue->next = new_node;
    }

    tail_client_socket_queue = new_node;
}

void *dequeue_client_socket() {
    if (head_client_socket_queue == NULL) {
        return NULL;
    }

    int *result = head_client_socket_queue->client_socket;
    ClientSocketQueueNode *temp = head_client_socket_queue;
    head_client_socket_queue = head_client_socket_queue->next;

    if (head_client_socket_queue == NULL) {
        tail_client_socket_queue = NULL;
    }

    free(temp);
    temp = NULL;

    return result;
}

void sigint_handler(int signo) {
    if (signo == SIGINT) {
        printf("\nReceived SIGINT, exiting program...\n");
        keep_running = 0;
    }
}
