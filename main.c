#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct ClientSocketQueueNode {
    struct ClientSocketQueueNode *next;
    int *client_socket;
} ClientSocketQueueNode;

typedef struct {
    char http_version[10];
    char url[255];
    char method[10];
    char *query_params;
    char *headers;
    char *body;
} HttpRequest;

void sigint_handler(int signo);
int router(void *p_client_socket, uint8_t thread_index);
void *thread_function(void *arg);
void enqueue_client_socket(int *client_socket);
void *dequeue_client_socket();
int home_get(int client_socket, HttpRequest *request, uint8_t thread_index);
int read_request(char **request_buffer, int client_socket);
int parse_http_request(HttpRequest *parsed_http_request, const char *http_request);
void http_request_free(HttpRequest *parsed_http_request);
int not_found(int client_socket, HttpRequest *request);

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
        fprintf(stderr, "Failed to create server socket\nError code: %d\n", errno);
        retval = -1;
        goto main_exit;
    }

    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) == -1) {
        fprintf(stderr, "Failed to set port address for immediately re-use after the socket is closed\nError code: %d\n", errno);
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
        fprintf(stderr, "Failed to bind server socket to adress(%d) and port(%d)\nError code: %d\n", server_addr.sin_addr.s_addr, server_addr.sin_port, errno);
        retval = -1;
        goto main_socket_cleanup;
    }

    if (listen(server_socket, MAX_CONNECTIONS) == -1) {
        fprintf(stderr, "Failed to set up server socker to listen for incoming connections\nError code: %d\n", errno);
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

    char *request = NULL;
    if (read_request(&request, client_socket) == -1) {
        retval = -1;
        goto cleanup_request_buffer;
    }

    if (strlen(request) == 0) {
        fprintf(stderr, "Request is empty\nError code: %d\n", errno);
        retval = 0;
        goto cleanup_request_buffer;
    }

    HttpRequest parsed_http_request = {0};
    if (parse_http_request(&parsed_http_request, request) == -1) {
        retval = -1;
        goto cleanup_parsed_request;
    }

    if (strcmp(parsed_http_request.url, "/") == 0) {
        if (strcmp(parsed_http_request.method, "GET") == 0) {
            if (home_get(client_socket, &parsed_http_request, thread_index) == -1) {
                retval = -1;
                goto cleanup_parsed_request;
            }

            goto cleanup_parsed_request;
        }
    } else {
        if (not_found(client_socket, &parsed_http_request) == -1) {
            retval = -1;
            goto cleanup_parsed_request;
        }

        goto cleanup_parsed_request;
    }

cleanup_parsed_request:
    http_request_free(&parsed_http_request);

cleanup_request_buffer:
    free(request);
    request = NULL;

    return retval;
}

int read_request(char **request_buffer, int client_socket) {
    size_t buffer_size = 1024;

    *request_buffer = (char *)malloc((buffer_size * (sizeof **request_buffer)) + 1);
    if (*request_buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory for *request_buffer\nError code: %d\n", errno);
        return -1;
    }

    (*request_buffer)[0] = '\0';

    size_t chunk_read = 0;
    int bytes_read;
    while ((bytes_read = recv(client_socket, (*request_buffer) + chunk_read, buffer_size - chunk_read, 0)) > 0) {
        chunk_read += bytes_read;

        if (chunk_read >= buffer_size) {
            buffer_size *= 2;
            *request_buffer = realloc((*request_buffer), buffer_size);
            if (*request_buffer == NULL) {
                fprintf(stderr, "Failed to reallocate memory for *request_buffer\nError code: %d\n", errno);
                free(*request_buffer);
                *request_buffer = NULL;
                return -1;
            }
        } else {
            break;
        }
    }

    if (bytes_read == -1) {
        fprintf(stderr, "Failed extract headers from *request_buffer\nError code: %d\n", errno);
        free(*request_buffer);
        *request_buffer = NULL;
        return -1;
    }

    (*request_buffer)[buffer_size] = '\0';

    return 0;
}

int parse_http_request(HttpRequest *parsed_http_request, const char *http_request) {
    // 1. Extract http request method
    const char *method_end = strchr(http_request, ' ');
    size_t method_length = method_end - http_request;
    if (memcpy(parsed_http_request->method, http_request, method_length) == NULL) {
        fprintf(stderr, "Failed to copy method from http request into structure\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    parsed_http_request->method[method_length] = '\0';

    // 2. Extract http request url and 3. url query params
    const char *url_start = method_end + 1;
    const char *url_end;
    if ((url_end = strchr(url_start, ' ')) == NULL) {
        fprintf(stderr, "Failed to find char\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    const char *query_params_start = url_start;

    int query_params_start_index = -1; // We start assuming there isn't any query params
    // Iterate until whitespace is found
    while (!isspace((unsigned char)*query_params_start)) {
        // If a '?' char is found along the way, that's the start of the query params
        if (*query_params_start == '?') {
            query_params_start_index = query_params_start - http_request;
            break;
        }

        // Keep moving forward
        query_params_start++;
    }

    size_t query_params_length = 0;
    if (query_params_start_index >= 0) {
        query_params_length = url_end - (query_params_start + 1); // Skip '?' char found at the beginning of query params

        // Because we separate the url and url query params, we set
        // the end of the url to where the url query params start
        url_end = &http_request[query_params_start_index];
    }

    size_t url_len = url_end - url_start;

    if (memcpy(parsed_http_request->url, url_start, url_len) == NULL) {
        fprintf(stderr, "Failed to copy url from http request into structure\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    parsed_http_request->url[url_len] = '\0';

    if (query_params_length > 0) {
        parsed_http_request->query_params = (char *)malloc(query_params_length * (sizeof parsed_http_request->query_params) + 1);
        if (parsed_http_request->query_params == NULL) {
            fprintf(stderr, "Failed to allocate memory for parsed_http_request->query_params\nError code: %d\n", errno);
            http_request_free(parsed_http_request);
            return -1;
        }

        // (query_params_start + 1) -> skip '?' char found at the beginning of query params
        if (memcpy(parsed_http_request->query_params, (query_params_start + 1), query_params_length) == NULL) {
            fprintf(stderr, "Failed to copy url query params from http request into structure\nError code: %d\n", errno);
            http_request_free(parsed_http_request);
            return -1;
        }

        parsed_http_request->query_params[query_params_length] = '\0';
    }

    // 4. Extract http version from request
    char *http_version_start;
    if ((http_version_start = strchr(query_params_start, ' ')) == NULL) {
        http_request_free(parsed_http_request);
        fprintf(stderr, "Failed to find char\nError code: %d\n", errno);
        return -1;
    }

    http_version_start++; // Skip space

    char *http_version_end;
    if ((http_version_end = strstr(http_version_start, "\r\n")) == NULL) {
        fprintf(stderr, "Failed to find string\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    size_t http_version_len = http_version_end - http_version_start;

    if (memcpy(parsed_http_request->http_version, http_version_start, http_version_len) == NULL) {
        fprintf(stderr, "Failed to copy http version from http request into structure\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    parsed_http_request->http_version[http_version_len] = '\0';

    // 5. Extract http request headers
    char *headers_start = http_version_end + 2; // Skip "\r\n"
    char *headers_end;
    if ((headers_end = strstr(headers_start, "\r\n\r\n")) == NULL) {
        fprintf(stderr, "Failed to find string\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    size_t headers_len = headers_end - headers_start;
    parsed_http_request->headers = (char *)malloc(headers_len * (sizeof parsed_http_request->headers) + 1);
    if (parsed_http_request->headers == NULL) {
        fprintf(stderr, "Failed to allocate memory for parsed_http_request->headers\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    if (memcpy(parsed_http_request->headers, headers_start, headers_len) == NULL) {
        fprintf(stderr, "Failed to copy headers from http request into structure\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    parsed_http_request->headers[headers_len] = '\0';

    // 6. Extract http request body
    char *body_start = headers_end + 4; // Skip "\r\n\r\n"
    char *body_end;
    if ((body_end = strchr(body_start, '\0')) == NULL) {
        fprintf(stderr, "Failed to find char\nError code: %d\n", errno);
        http_request_free(parsed_http_request);
        return -1;
    }

    size_t body_length = body_end - body_start;
    if (body_length > 0) {
        parsed_http_request->body = (char *)malloc(body_length * (sizeof parsed_http_request->body) + 1);
        if (parsed_http_request->body == NULL) {
            fprintf(stderr, "Failed to allocate memory for parsed_http_request->body\nError code: %d\n", errno);
            http_request_free(parsed_http_request);
            return -1;
        }

        if (memcpy(parsed_http_request->body, body_start, body_length) == NULL) {
            fprintf(stderr, "Failed to copy request body from http request into structure\nError code: %d\n", errno);
            http_request_free(parsed_http_request);
            return -1;
        }

        parsed_http_request->body[body_length] = '\0';
    }

    return 0;
}

void http_request_free(HttpRequest *parsed_http_request) {
    free(parsed_http_request->query_params);
    parsed_http_request->query_params = NULL;

    free(parsed_http_request->body);
    parsed_http_request->body = NULL;

    free(parsed_http_request->headers);
    parsed_http_request->headers = NULL;
}

int home_get(int client_socket, HttpRequest *request, uint8_t thread_index) {
    printf("%s\n", request->url);

    char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                      "<html><body><h1>Hello world!</h1></body></html>";

    printf("It works!!! (on thread %d)\n", thread_index);

    if (send(client_socket, response, strlen(response), 0) == -1) {
        fprintf(stderr, "Failed send HTTP response\nError code: %d\n", errno);
        close(client_socket);
        return -1;
    }

    close(client_socket);
    return 0;
}

int not_found(int client_socket, HttpRequest *request) {
    printf("%s\n", request->url);

    char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                      "<html><body><h1>404 Not Found</h1></body></html>";

    if (send(client_socket, response, strlen(response), 0) == -1) {
        fprintf(stderr, "Failed send HTTP response\nError code: %d\n", errno);
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
