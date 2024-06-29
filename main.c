#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <libpq-fe.h>
#include <linux/limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/**
 * In development mode, set PANIC to 1 to force program crash on error.
 * In production, set PANIC to 0 and try to recover from error.
 * TODO: This variable should be set at compile time, not hardcoded.
 */
#define PANIC 1

#define MAX_LINE_LENGTH 100
#define MAX_LENGTH_ABSOLUTE_PATH 200
#define MAX_LENGTH_RELATIVE_PATH 100
#define MAX_LENGTH_COMPONENT_NAME 100

#define ROWS 4

#define MAX_PROJECT_ROOT_PATH_LENGTH 100
#define PROJECT_ROOT_PATH "/workspaces/web-server/"

typedef struct ClientSocketQueueNode {
    struct ClientSocketQueueNode *next;
    int *client_socket;
} ClientSocketQueueNode;

typedef struct {
    char message[255];
    int8_t panic;
    /** TODO: add more fields here that can help to reproduce the error easily */
} Error;

typedef struct {
    char DB_NAME[12];
    char DB_USER[16];
    char DB_PASSWORD[9];
    char DB_HOST[10];
    char DB_PORT[5];
} ENV;

typedef struct {
    char http_version[10];
    char url[255];
    char method[10];
    char *query_params;
    char *headers;
    char *body;
} HttpRequest;

typedef struct Template Template;

#define MAX_LENGTH_BLOCK_NAME 100
typedef struct {
    enum { FOR, IF } type;
    char name[MAX_LENGTH_BLOCK_NAME];
    struct {
        unsigned int start;
        unsigned int end;
        char *content;
    } html;
    struct {
        unsigned int start;
        unsigned int end;
        char *reference;
    } lookup;
} ActionBlock;

typedef struct {
    struct {
        unsigned int start;
        unsigned int end;
        char *reference;
    } lookup;
    Template *component;
} Component;

struct Template {
    char path[MAX_LENGTH_ABSOLUTE_PATH];
    char *html_content;
    size_t html_content_length;
    ActionBlock *blocks;
    uint8_t blocks_length;
    Component **components;
    uint8_t components_length;
};

/* clang-format off */
#define STATUS_PATH PROJECT_ROOT_PATH "status.html"
Template status = {STATUS_PATH, NULL, (112 + 1), NULL, 0, NULL, 0};

Component __component_status = {{113, 146, NULL}, &status};

#define MAX_BUTTON_COMPONENTS 1
Component *button__components[MAX_BUTTON_COMPONENTS] = {
    &__component_status,
};

#define BUTTON_PATH PROJECT_ROOT_PATH "button.html"
Template button = {BUTTON_PATH, NULL, (162 + 1), NULL, 0, button__components, MAX_BUTTON_COMPONENTS};

#define MAX_DEFAULT_PAGE_BLOCKS 2
ActionBlock default_page__blocks[MAX_DEFAULT_PAGE_BLOCKS] = {
    {FOR, "clickable_buttons", {774, 818, NULL}, {738, 836, NULL}}, 
};

Component __component_button = {{779, 812, NULL}, &button};

#define MAX_DEFAULT_PAGE_COMPONENTS 1
Component *default_page__components[MAX_DEFAULT_PAGE_COMPONENTS] = {
    &__component_button,
};

#define DEFAULT_PAGE_PATH PROJECT_ROOT_PATH "default.html"
Template default_page = {DEFAULT_PAGE_PATH, NULL, (857 + 1), default_page__blocks, MAX_DEFAULT_PAGE_BLOCKS, default_page__components, MAX_DEFAULT_PAGE_COMPONENTS};

#define MAX_TEMPLATES 1
Template *templates[MAX_TEMPLATES] = {
    &default_page,
};
/* clang-format on */

void sigint_handler(int signo);
Error router(void *p_client_socket, uint8_t thread_index);
void *thread_function(void *arg);
void enqueue_client_socket(int *client_socket);
void *dequeue_client_socket();
Error home_get(int client_socket, HttpRequest *request, uint8_t thread_index);
Error read_request(char **request_buffer, int client_socket);
Error parse_http_request(HttpRequest *parsed_http_request, const char *http_request);
void http_request_free(HttpRequest *parsed_http_request);
Error not_found(int client_socket, HttpRequest *request);
Error load_values_from_file(void *structure, const char *project_root_path, const char *file_path_relative_to_project_root);
void print_query_result(PGresult *query_result);
Error read_file(char **buffer, char *absolute_file_path, size_t file_size);
Error calculate_file_size(long *file_size, char *absolute_file_path);

#define MAX_CONNECTIONS 100
#define POOL_SIZE 1

volatile sig_atomic_t keep_running = 1;

PGconn *conn_pool[POOL_SIZE];

pthread_t thread_pool[POOL_SIZE];
pthread_mutex_t thread_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t thread_condition_var = PTHREAD_COND_INITIALIZER;

ClientSocketQueueNode *head_client_socket_queue = NULL;
ClientSocketQueueNode *tail_client_socket_queue = NULL;

int main() {
    int retval = 0;
    uint8_t i;

    Error error = {0};

    /**
     * Registers a signal handler for SIGINT (to terminate the process)
     * to exit the program gracefully for Valgrind to show the program report.
     */
    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
        fprintf(stderr, "Failed to set up signal handler for SIGINT\nError code: %d\n", errno);
        retval = -1;
        goto main_exit;
    }

    /* ====================================================================== */
    /*                   RESOLVE TEMPLATES AND ACTION BLOCKS                  */
    /* ====================================================================== */

    /*****************************[ DEFAULT PAGE ]*****************************/

    default_page.html_content = (char *)calloc(default_page.html_content_length, sizeof(char));
    if (default_page.html_content == NULL) {
        fprintf(stderr, "Failed to allocate memory for default_page.html_content\nError code: %d\n", errno);
        /** TODO */
    }

    if ((error = read_file(&(default_page.html_content), default_page.path, (default_page.html_content_length - 1))).panic) {
        /** TODO */
    }

    default_page.html_content[default_page.html_content_length - 1] = '\0';

    for (i = 0; i < default_page.blocks_length; i++) {
        size_t block_html_content_length = (default_page.blocks[i].html.end - default_page.blocks[i].html.start) + 1;

        default_page.blocks[i].html.content = (char *)calloc(block_html_content_length, sizeof(char));
        if (default_page.blocks[i].html.content == NULL) {
            fprintf(stderr, "Failed to allocate memory for default_page.blocks[i].html.content\nError code: %d\n", errno);
            /** TODO */
        }

        if (memcpy(default_page.blocks[i].html.content, default_page.html_content + default_page.blocks[i].html.start, (block_html_content_length - 1)) == NULL) {
            /** TODO */
        }

        default_page.blocks[i].html.content[block_html_content_length - 1] = '\0';

        size_t block_lookup_reference_length = (default_page.blocks[i].lookup.end - default_page.blocks[i].lookup.start) + 1;

        default_page.blocks[i].lookup.reference = (char *)calloc(block_lookup_reference_length, sizeof(char));
        if (default_page.blocks[i].lookup.reference == NULL) {
            fprintf(stderr, "Failed to allocate memory for default_page.blocks[i].lookup.reference\nError code: %d\n", errno);
            /** TODO */
        }

        if (memcpy(default_page.blocks[i].lookup.reference, default_page.html_content + default_page.blocks[i].lookup.start, (block_lookup_reference_length - 1)) == NULL) {
            /** TODO */
        }

        default_page.blocks[i].lookup.reference[block_lookup_reference_length - 1] = '\0';
    }

    /**************************[  BUTTON COMPONENT ]***************************/

    button.html_content = (char *)calloc(button.html_content_length, sizeof(char));
    if (button.html_content == NULL) {
        fprintf(stderr, "Failed to allocate memory for button.html_content\nError code: %d\n", errno);
        /** TODO */
    }

    if ((error = read_file(&(button.html_content), button.path, (button.html_content_length - 1))).panic) {
        /** TODO */
    }

    button.html_content[button.html_content_length - 1] = '\0';

    /**************************[  STATUS COMPONENT ]***************************/

    status.html_content = (char *)calloc(status.html_content_length, sizeof(char));
    if (status.html_content == NULL) {
        fprintf(stderr, "Failed to allocate memory for status.html_content\nError code: %d\n", errno);
        /** TODO */
    }

    if ((error = read_file(&(status.html_content), status.path, (status.html_content_length - 1))).panic) {
        /** TODO */
    }

    status.html_content[status.html_content_length - 1] = '\0';

    /* ====================================================================== */
    /*                        RESOLVE TEMPLATE COMPONENTS                     */
    /* ====================================================================== */

    /*****************************[ DEFAULT PAGE ]*****************************/

    for (i = 0; i < default_page.components_length; i++) {
        size_t l1_component_lookup_reference_length = (default_page.components[i]->lookup.end - default_page.components[i]->lookup.start) + 1;

        default_page.components[i]->lookup.reference = (char *)calloc(l1_component_lookup_reference_length, sizeof(char));
        if (default_page.components[i]->lookup.reference == NULL) {
            fprintf(stderr, "Failed to allocate memory for default_page.components[i]->lookup.reference\nError code: %d\n", errno);
            /** TODO */
        }

        if (memcpy(default_page.components[i]->lookup.reference, default_page.html_content + default_page.components[i]->lookup.start, (l1_component_lookup_reference_length - 1)) == NULL) {
            /** TODO */
        }

        default_page.components[i]->lookup.reference[l1_component_lookup_reference_length - 1] = '\0';

        uint8_t j;
        for (j = 0; j < default_page.components[i]->component->components_length; j++) {
            size_t l2_component_lookup_reference_length = (default_page.components[i]->component->components[j]->lookup.end - default_page.components[i]->component->components[j]->lookup.start) + 1;

            default_page.components[i]->component->components[j]->lookup.reference = (char *)calloc(l2_component_lookup_reference_length, sizeof(char));
            if (default_page.components[i]->component->components[j]->lookup.reference == NULL) {
                fprintf(stderr, "Failed to allocate memory for default_page.components[i]->component->components[j]->lookup.reference\nError code: %d\n", errno);
                /** TODO */
            }

            if (memcpy(default_page.components[i]->component->components[j]->lookup.reference, default_page.components[i]->component->html_content + default_page.components[i]->component->components[j]->lookup.start, (l2_component_lookup_reference_length - 1)) == NULL) {
                /** TODO */
            }

            default_page.components[i]->component->components[j]->lookup.reference[l2_component_lookup_reference_length - 1] = '\0';
        }
    }

    ENV env = {0};
    const char env_file_path[] = ".env";
    if ((error = load_values_from_file(&env, PROJECT_ROOT_PATH, env_file_path)).panic) {
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

    int optname = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &optname, sizeof(int)) == -1) {
        fprintf(stderr, "Failed to set port address for immediately re-use after the socket is closed\nError code: %d\n", errno);
        retval = -1;
        goto main_cleanup_socket;
    }

    /** Configure server address */
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;         /** IPv4 */
    server_addr.sin_port = htons(port);       /** Convert the port number from host byte order to network byte order (big-endian) */
    server_addr.sin_addr.s_addr = INADDR_ANY; /** Listen on all available network interfaces (IPv4 addresses) */

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        fprintf(stderr, "Failed to bind server socket to adress(%d) and port(%d)\nError code: %d\n", server_addr.sin_addr.s_addr, server_addr.sin_port, errno);
        retval = -1;
        goto main_cleanup_socket;
    }

    if (listen(server_socket, MAX_CONNECTIONS) == -1) {
        fprintf(stderr, "Failed to set up server socker to listen for incoming connections\nError code: %d\n", errno);
        retval = -1;
        goto main_cleanup_socket;
    }

    printf("Server listening on port: %d...\n", port);

    const char *db_connection_keywords[] = {"dbname", "user", "password", "host", "port", NULL};
    const char *db_connection_values[6];
    db_connection_values[0] = env.DB_NAME;
    db_connection_values[1] = env.DB_USER;
    db_connection_values[2] = env.DB_PASSWORD;
    db_connection_values[3] = env.DB_HOST;
    db_connection_values[4] = env.DB_PORT;
    db_connection_values[5] = NULL;

    /** Create thread pool */
    for (i = 0; i < POOL_SIZE; i++) {
        /**
         * Thread might take some time time to create, but 'i' will keep mutating as the program runs,
         * storing the value of 'i' in the heap at the time of iterating ensures that thread_function
         * receives the correct value even when 'i' has moved on.
         */
        unsigned short *p_iteration = malloc(sizeof(unsigned short));
        *p_iteration = i;
        if (pthread_create(&thread_pool[*p_iteration], NULL, &thread_function, p_iteration) != 0) {
            fprintf(stderr, "Failed to create thread at iteration n° %d\nError code: %d\n", *p_iteration, errno);
            retval = -1;
            goto main_cleanup_threads;
        }
    }

    for (i = 0; i < POOL_SIZE; i++) {
        conn_pool[i] = PQconnectdbParams(db_connection_keywords, db_connection_values, 0);
        if (PQstatus(conn_pool[i]) != CONNECTION_OK) {
            fprintf(stderr, "Failed to create db connection pool at iteration n° %d\nError code: %d\n", i, errno);
            retval = -1;
            goto main_cleanup;
        }
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof client_addr;

        /** The while loop will wait at accept for a new client to connect */
        int client_socket;
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);

        /**
         * When a signal to exit the program is received, 'accept' will error with -1. To verify that
         * client_socket being -1 isn't because of program termination, check whether the program
         * has received a signal to exit.
         */
        if (keep_running == 0) {
            /** Send signal to threads to resume execusion so they can proceed to cleanup */
            pthread_cond_signal(&thread_condition_var);
            retval = 0;
            goto main_cleanup;
        }

        /** At this point, we know that client_socket being -1 wouldn't have been caused by program termination */
        if (client_socket == -1) {
            fprintf(stderr, "Failed to create client socket\nError code: %d\n", errno);
            retval = -1;
            goto main_cleanup;
        }

        /**
         * Store fd number for client socket in the heap so it can be pointed by a queue data structure
         * and used when it is needed.
         */
        int *p_client_socket = malloc(sizeof(int));
        *p_client_socket = client_socket;

        if (pthread_mutex_lock(&thread_mutex) != 0) {
            fprintf(stderr, "Failed to lock pthread mutex\nError code: %d\n", errno);
            retval = -1;
            goto main_cleanup;
        }

        printf("- New request: enqueue_client_socket client socket fd %d\n", *p_client_socket);
        enqueue_client_socket(p_client_socket);

        if (pthread_cond_signal(&thread_condition_var) != 0) {
            fprintf(stderr, "Failed to send pthread signal\nError code: %d\n", errno);
            retval = -1;
            goto main_cleanup;
        }

        if (pthread_mutex_unlock(&thread_mutex) != 0) {
            fprintf(stderr, "Failed to unlock pthread mutex\nError code: %d\n", errno);
            retval = -1;
            goto main_cleanup;
        }
    }

main_cleanup:
    for (i = 0; i < POOL_SIZE; i++) {
        PQfinish(conn_pool[i]);
    }

main_cleanup_threads:
    for (i = 0; i < POOL_SIZE; i++) {
        /**
         * At this point, the variable 'keep_running' is 0.
         * Signal all threads to resume execution to perform cleanup.
         */
        pthread_cond_signal(&thread_condition_var);
    }

    for (i = 0; i < POOL_SIZE; i++) {
        printf("(Thread %d) Cleaning up thread %lu\n", i, thread_pool[i]);

        if (pthread_join(thread_pool[i], NULL) != 0) {
            fprintf(stderr, "Failed to join thread at position %d in the thread pool\nError code: %d\n", i, errno);
        }
    }

main_cleanup_socket:
    close(server_socket);

main_exit:
    printf("%s\n", error.message);

    /* TODO: Free templates */

    return retval;
}

void *thread_function(void *arg) {
    Error error = {0};
    uint8_t *p_thread_index = (uint8_t *)arg;
    pthread_t tid = pthread_self();
    printf("(Thread %d) Setting up thread %lu\n", *p_thread_index, tid);

    while (1) {
        int *p_client_socket;

        if (pthread_mutex_lock(&thread_mutex) != 0) {
            /** TODO: cleanup */
        }

        /** Check queue for client request */
        if ((p_client_socket = dequeue_client_socket()) == NULL) {
            /** At this point, we know the queue was empty, so hold thread execusion */
            pthread_cond_wait(&thread_condition_var, &thread_mutex); /** On hold, waiting to receive a signal... */
            /** Signal to proceed with execusion has been sent */

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
            if ((error = router(p_client_socket, *p_thread_index)).panic) {
                break;
            }
        }
    }

out:
    printf("(Thread %d) Out of while loop\n", *p_thread_index);

    free(p_thread_index);
    p_thread_index = NULL;

    return NULL;
}

Error router(void *p_client_socket, uint8_t thread_index) {
    Error error = {0};

    /**
     * At this point we don't need to hold the client_socket in heap anymore, we can work
     * with it in the stack from now on
     */
    int client_socket = *((int *)p_client_socket);

    free(p_client_socket);
    p_client_socket = NULL;

    char *request = NULL;
    if ((error = read_request(&request, client_socket)).panic) {
        goto router_exit;
    }

    if (strlen(request) == 0) {
        printf("Request is empty\n");
        goto router_cleanup_request;
    }

    HttpRequest parsed_http_request = {0};
    if ((error = parse_http_request(&parsed_http_request, request)).panic) {
        goto router_cleanup_request;
    }

    if (strcmp(parsed_http_request.url, "/") == 0) {
        if (strcmp(parsed_http_request.method, "GET") == 0) {
            error = home_get(client_socket, &parsed_http_request, thread_index);
            goto router_cleanup_parsed_request;
        }
    } else {
        error = not_found(client_socket, &parsed_http_request);
        goto router_cleanup_parsed_request;
    }

router_cleanup_parsed_request:
    http_request_free(&parsed_http_request);

router_cleanup_request:
    free(request);
    request = NULL;

router_exit:
    return error;
}

Error read_request(char **request_buffer, int client_socket) {
    Error error = {0};
    size_t buffer_size = 1024;

    *request_buffer = (char *)calloc(buffer_size + 1, sizeof(char));
    if (*request_buffer == NULL) {
        sprintf(error.message, "Failed to allocate memory for *request_buffer. Error code: %d", errno);
        error.panic = 1;
        return error;
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
                free(*request_buffer);
                *request_buffer = NULL;
                sprintf(error.message, "Failed to reallocate memory for *request_buffer\nError code: %d", errno);
                error.panic = 1;
                return error;
            }
        } else {
            break;
        }
    }

    if (bytes_read == -1) {
        free(*request_buffer);
        *request_buffer = NULL;
        sprintf(error.message, "Failed extract headers from *request_buffer\nError code: %d", errno);
        error.panic = 1;
        return error;
    }

    (*request_buffer)[buffer_size] = '\0';

    return error;
}

Error parse_http_request(HttpRequest *parsed_http_request, const char *http_request) {
    Error error = {0};

    /** 1. Extract http request method */
    const char *method_end = strchr(http_request, ' ');
    size_t method_length = method_end - http_request;
    if (memcpy(parsed_http_request->method, http_request, method_length) == NULL) {
        sprintf(error.message, "Failed to copy method from http request into structure. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    parsed_http_request->method[method_length] = '\0';

    /** 2. Extract http request url and 3. url query params */
    const char *url_start = method_end + 1;
    const char *url_end;
    if ((url_end = strchr(url_start, ' ')) == NULL) {
        sprintf(error.message, "Failed to find char. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    const char *query_params_start = url_start;

    int query_params_start_index = -1; /** We start assuming there isn't any query params */
    /** Iterate until whitespace is found */
    while (!isspace((unsigned char)*query_params_start)) {
        /** If a '?' char is found along the way, that's the start of the query params */
        if (*query_params_start == '?') {
            query_params_start_index = query_params_start - http_request;
            break;
        }

        /** Keep moving forward */
        query_params_start++;
    }

    size_t query_params_length = 0;
    if (query_params_start_index >= 0) {
        query_params_length = url_end - (query_params_start + 1); /** Skip '?' char found at the beginning of query params */

        /**
         * Because we separate the url and url query params, we set
         * the end of the url to where the url query params start
         */
        url_end = &http_request[query_params_start_index];
    }

    size_t url_len = url_end - url_start;

    if (memcpy(parsed_http_request->url, url_start, url_len) == NULL) {
        sprintf(error.message, "Failed to copy url from http request into structure. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    parsed_http_request->url[url_len] = '\0';

    if (query_params_length > 0) {
        parsed_http_request->query_params = (char *)calloc(query_params_length + 1, sizeof(char));
        if (parsed_http_request->query_params == NULL) {
            sprintf(error.message, "Failed to allocate memory for parsed_http_request->query_params. Error code: %d", errno);
            error.panic = 1;
            http_request_free(parsed_http_request);
            return error;
        }

        /** (query_params_start + 1) -> skip '?' char found at the beginning of query params */
        if (memcpy(parsed_http_request->query_params, (query_params_start + 1), query_params_length) == NULL) {
            sprintf(error.message, "Failed to copy url query params from http request into structure. Error code: %d", errno);
            error.panic = 1;
            http_request_free(parsed_http_request);
            return error;
        }

        parsed_http_request->query_params[query_params_length] = '\0';
    }

    /** 4. Extract http version from request */
    char *http_version_start;
    if ((http_version_start = strchr(query_params_start, ' ')) == NULL) {
        sprintf(error.message, "Failed to find char. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    http_version_start++; /** Skip space */

    char *http_version_end;
    if ((http_version_end = strstr(http_version_start, "\r\n")) == NULL) {
        sprintf(error.message, "Failed to find string. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    size_t http_version_len = http_version_end - http_version_start;

    if (memcpy(parsed_http_request->http_version, http_version_start, http_version_len) == NULL) {
        sprintf(error.message, "Failed to copy http version from http request into structure. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    parsed_http_request->http_version[http_version_len] = '\0';

    /** 5. Extract http request headers */
    char *headers_start = http_version_end + 2; /** Skip "\r\n" */
    char *headers_end;
    if ((headers_end = strstr(headers_start, "\r\n\r\n")) == NULL) {
        sprintf(error.message, "Failed to find string. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    size_t headers_len = headers_end - headers_start;
    parsed_http_request->headers = (char *)calloc(headers_len + 1, sizeof(char));
    if (parsed_http_request->headers == NULL) {
        sprintf(error.message, "Failed to allocate memory for parsed_http_request->headers. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    if (memcpy(parsed_http_request->headers, headers_start, headers_len) == NULL) {
        sprintf(error.message, "Failed to copy headers from http request into structure. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    parsed_http_request->headers[headers_len] = '\0';

    /** 6. Extract http request body */
    char *body_start = headers_end + 4; /** Skip "\r\n\r\n" */
    char *body_end;
    if ((body_end = strchr(body_start, '\0')) == NULL) {
        sprintf(error.message, "Failed to find char. Error code: %d", errno);
        error.panic = 1;
        http_request_free(parsed_http_request);
        return error;
    }

    size_t body_length = body_end - body_start;
    if (body_length > 0) {
        parsed_http_request->body = (char *)calloc(body_length + 1, sizeof(char));
        if (parsed_http_request->body == NULL) {
            sprintf(error.message, "Failed to allocate memory for parsed_http_request->body. Error code: %d", errno);
            error.panic = 1;
            http_request_free(parsed_http_request);
            return error;
        }

        if (memcpy(parsed_http_request->body, body_start, body_length) == NULL) {
            sprintf(error.message, "Failed to copy request body from http request into structure. Error code: %d", errno);
            error.panic = 1;
            http_request_free(parsed_http_request);
            return error;
        }

        parsed_http_request->body[body_length] = '\0';
    }

    return error;
}

void http_request_free(HttpRequest *parsed_http_request) {
    free(parsed_http_request->query_params);
    parsed_http_request->query_params = NULL;

    free(parsed_http_request->body);
    parsed_http_request->body = NULL;

    free(parsed_http_request->headers);
    parsed_http_request->headers = NULL;
}

Error home_get(int client_socket, HttpRequest *request, uint8_t thread_index) {
    Error error = {0};

    PGconn *conn = conn_pool[thread_index];

    const char query[] = "SELECT u.id, u.email, c.nicename AS country, CONCAT(ui.first_name, ' ', ui.last_name) AS full_name FROM app.users u JOIN app.users_info ui ON u.id = ui.user_id JOIN app.countries c ON ui.country_id = c.id";
    PGresult *users_result = PQexec(conn, query);

    if (PQresultStatus(users_result) != PGRES_TUPLES_OK) {
        sprintf(error.message, "%s\nError code: %d\n", PQerrorMessage(conn), errno);
        error.panic = PANIC;
        return error;
    }

    /*
    print_query_result(users_result);
    */

    printf("Template: %s\n", templates[0]->path);

    char *default_page_working_copy = (char *)calloc(templates[0]->html_content_length, sizeof(char));
    if (default_page_working_copy == NULL) {
        /* TODO */
    }

    if (memcpy(default_page_working_copy, templates[0]->html_content, (templates[0]->html_content_length - 1)) == NULL) {
        /* TODO */
    }

    default_page_working_copy[templates[0]->html_content_length - 1] = '\0';

    printf("%s\n", default_page_working_copy);
    printf("Has %d components\n", templates[0]->components_length);

    uint8_t i;
    for (i = 0; i < templates[0]->components_length; i++) {
        printf("Component: %s\n", templates[0]->components[i]->component->path);

        char *l1_component_working_copy = (char *)calloc(templates[0]->components[i]->component->html_content_length, sizeof(char));
        if (l1_component_working_copy == NULL) {
            /* TODO */
        }

        if (memcpy(l1_component_working_copy, templates[0]->components[i]->component->html_content, (templates[0]->components[i]->component->html_content_length - 1)) == NULL) {
            /* TODO */
        }

        l1_component_working_copy[templates[0]->components[i]->component->html_content_length - 1] = '\0';

        printf("%s\n", l1_component_working_copy);
        printf("Has %d components\n", templates[0]->components[i]->component->components_length);

        uint8_t j;
        for (j = 0; j < templates[0]->components[i]->component->components_length; j++) {
            printf("Component: %s\n", templates[0]->components[i]->component->components[j]->component->path);

            char *l2_component_working_copy = (char *)calloc(templates[0]->components[i]->component->components[j]->component->html_content_length, sizeof(char));
            if (l2_component_working_copy == NULL) {
                /* TODO */
            }

            if (memcpy(l2_component_working_copy, templates[0]->components[i]->component->components[j]->component->html_content, (templates[0]->components[i]->component->components[j]->component->html_content_length - 1)) == NULL) {
                /* TODO */
            }

            l2_component_working_copy[templates[0]->components[i]->component->components[j]->component->html_content_length - 1] = '\0';

            printf("%s\n", l2_component_working_copy);
            printf("Has %d components\n", templates[0]->components[i]->component->components[j]->component->components_length);

            char *after_address;
            if ((after_address = strstr(l1_component_working_copy, templates[0]->components[i]->component->components[j]->lookup.reference)) == NULL) {
                /* TODO */
            }

            size_t component_lookup_reference_length = templates[0]->components[i]->component->components[j]->lookup.end - templates[0]->components[i]->component->components[j]->lookup.start;

            size_t l1_component_child_component_reference_start = after_address - l1_component_working_copy;
            after_address += component_lookup_reference_length;
            size_t after_length = strlen(after_address) + 1;

            printf("%s\n", after_address);

            char *after = (char *)calloc(after_length, sizeof(char));
            if (after == NULL) {
                /* TODO */
            }

            after[after_length - 1] = '\0';

            if (memcpy(after, after_address, after_length) == NULL) {
                /* TODO */
            }

            l1_component_working_copy = (char *)realloc(l1_component_working_copy, (l1_component_child_component_reference_start + strlen(l2_component_working_copy) + after_length) * sizeof(char));
            if (l1_component_working_copy == NULL) {
                /* TODO */
            }

            memmove(l1_component_working_copy + l1_component_child_component_reference_start + strlen(l2_component_working_copy), after, after_length);
            memcpy(l1_component_working_copy + l1_component_child_component_reference_start, l2_component_working_copy, strlen(l2_component_working_copy));

            printf("%s\n", l1_component_working_copy);
        }
    }

    /**
     * How to render a block inside a html template?
     * 1. A template has been redendered with several blocks already so addresses are useless at this point
     * 2. Use the reference lookup to find the end of there the new block will be put
     * 3. Count from the end of the block to the end of the template and that's the 'after'
     */

    char response[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
                      "<html><body><h1>Hello world!</h1></body></html>";

    if (send(client_socket, response, strlen(response), 0) == -1) {
        sprintf(error.message, "Failed send HTTP response. Error code: %d", errno);
        error.panic = PANIC;
        close(client_socket);
        return error;
    }

    close(client_socket);
    return error;
}

Error not_found(int client_socket, HttpRequest *request) {
    Error error = {0};

    char response[] = "HTTP/1.1 404 Not Found\r\nContent-Type: text/html\r\n\r\n"
                      "<html><body><h1>404 Not Found</h1></body></html>";

    if (send(client_socket, response, strlen(response), 0) == -1) {
        sprintf(error.message, "Failed send HTTP response. Error code: %d", errno);
        error.panic = PANIC;
        close(client_socket);
        return error;
    }

    close(client_socket);
    return error;
}

/**
 * @brief      Read values from a file into a structure. Ignores lines that start with '#' (comments).
 *             The values in the file must adhere to the following rules:
 *                  1. Each value must be on a new line.
 *                  2. Values must adhere to the specified order and length defined in the structure
 *                     they are loaded into.
 *                  3. values must start inmediately after the equal sign and be contiguous.
 *                     e.g. =helloworld
 *
 * @param[out] structure A structure with fixed-size string fields for each value in the file.
 * @param      project_root_path The path to project root.
 * @param      file_path_relative_to_project_root The path to the file from project root.
 * @return     Error information if an error occurs.
 */
Error load_values_from_file(void *structure, const char *project_root_path, const char *file_path_relative_to_project_root) {
    Error error = {0};

    char file_absolute_path[MAX_LENGTH_ABSOLUTE_PATH];
    if (sprintf(file_absolute_path, "%s/%s", project_root_path, file_path_relative_to_project_root) < 0) {
        sprintf(error.message, "Absolute path truncated. Error code: %d", errno);
        error.panic = 1;
        return error;
    }

    FILE *file = fopen(file_absolute_path, "r");

    if (file == NULL) {
        sprintf(error.message, "Failed to open file. Error code: %d", errno);
        error.panic = 1;
        return error;
    }

    char line[MAX_LINE_LENGTH];

    size_t structure_element_offset = 0;

    unsigned int read_values_count = 0;

    while (fgets(line, MAX_LINE_LENGTH, file) != NULL) {
        /** Does line have a comment? */
        char *hash_position = strchr(line, '#');
        if (hash_position != NULL) {
            /** We don't care about comments. Truncate line at the start of a comment */
            *hash_position = '\0';
        }

        /** The value we want to read starts after an '=' sign */
        char *equal_sign = strchr(line, '=');
        if (equal_sign == NULL) {
            /** This line does not contain a value, continue to next line */
            continue;
        }

        equal_sign++; /** Move the pointer past the '=' character to the beginning of the value */

        size_t value_char_index = 0;

        /** Extract 'value characters' until a 'whitespace' character or null-terminator is encountered */
        while (*equal_sign != '\0' && !isspace((unsigned char)*equal_sign)) {
            ((char *)structure)[structure_element_offset + value_char_index] = *equal_sign;
            value_char_index++;
            equal_sign++;
        }

        ((char *)structure)[structure_element_offset + value_char_index] = '\0';

        /** Next element in the structure should start after the read value null-terminator */
        structure_element_offset += value_char_index + 1;
        read_values_count++;
    }

    fclose(file);
    return error;
}

Error read_file(char **buffer, char *absolute_file_path, size_t file_size) {
    Error error = {0};

    FILE *file = fopen(absolute_file_path, "r");

    if (file == NULL) {
        sprintf(error.message, "Failed to open file. Error code: %d", errno);
        error.panic = PANIC;
        return error;
    }

    if (fread(*buffer, sizeof(char), file_size, file) != file_size) {
        if (feof(file)) {
            sprintf(error.message, "End of file reached before reading all elements. Error code: %d", errno);
        }

        if (ferror(file)) {
            sprintf(error.message, "An error occurred during the fread operation. Error code: %d", errno);
        }

        fclose(file);

        error.panic = PANIC;
        return error;
    }

    fclose(file);

    return error;
}

Error calculate_file_size(long *file_size, char *absolute_file_path) {
    Error error = {0};

    FILE *file = fopen(absolute_file_path, "r");

    if (file == NULL) {
        sprintf(error.message, "Failed to open file. Error code: %d", errno);
        error.panic = PANIC;
        return error;
    }

    if (fseek(file, 0, SEEK_END) == -1) {
        fclose(file);
        sprintf(error.message, "Failed to move the 'file position indicator' to the end of the file. Error code: %d", errno);
        error.panic = PANIC;
        return error;
    }

    *file_size = ftell(file);
    if (*file_size == -1) {
        fclose(file);
        sprintf(error.message, "Failed to determine the current 'file position indicator' of the file. Error code: %d", errno);
        error.panic = PANIC;
        return error;
    }

    rewind(file);
    fclose(file);

    return error;
}

void print_query_result(PGresult *query_result) {
    const int num_columns = PQnfields(query_result);
    const int num_rows = PQntuples(query_result);

    int col = 0;
    int row = 0;
    int i = 0;

    for (col = 0; col < num_columns; col++) {
        printf("| %-48s ", PQfname(query_result, col));
    }
    printf("|\n");

    printf("|");
    for (col = 0; col < num_columns; col++) {
        for (i = 0; i < 50; i++) {
            printf("-");
        }
        printf("|");
    }
    printf("\n");

    for (row = 0; row < num_rows; row++) {
        for (col = 0; col < num_columns; col++) {
            printf("| %-48s ", PQgetvalue(query_result, row, col));
        }
        printf("|\n");
    }

    printf("\n");
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
