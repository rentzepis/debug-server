/**
 * @author Jonathan Helland
 *
 * Multithreaded web proxy with caching.
 *
 * The cache is implemented using a hash table (using Robin Hood hashing) and a
 * doubly linked circular list for the LRU eviction policy. Cache access is
 * synchronized using a LIFO queue with a mutex.
 *
 * Known bug: sometimes objects will be evicted from the cache before they are
 * finished being referenced. I cannot for the life of me figure out why -- the
 * whole point of the LIFO queue was to track the count of readers and prevent
 * any writes (which cause evictions) while readers are still accessing the
 * cache.
 */

#include "csapp.h"
#include "http_parser.h"
#include "cache.h"

#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/**************** STRUCTS, TYPES, & ENUMS ****************/
/**
 * Convenient shorthand for socket addresses.
 */
typedef struct sockaddr SA;

/**
 * @brief Struct to hold user-specified options for runtime.
 */
typedef struct {
    bool verbose; /* Display errors, primarily. */
    char *port;   /* Port to listen to for client connections. */
} cfg_t;

/**
 * Copied from tiny.c
 * Holds client connection metadata.
 */
#define HOSTLEN 256
#define SERVLEN 8
typedef struct {
    struct sockaddr_in addr;
    socklen_t addrlen;
    int connfd;
    char host[HOSTLEN];
    char serv[SERVLEN];
} client_info_t;

/**
 * Struct that holds a parsed client request.
 */
typedef struct {
    const char *method;
    const char *host;
    const char *scheme;
    const char *uri;
    const char *port;
    const char *path;
    const char *http_version;
} request_t;

/**
 * Error codes associated with parsing a client request.
 */
typedef enum {
    OK,                /* No error occurred. */
    PARSER_ERROR,      /* parser_t encountered an error. */
    ERROR_501,         /* Not implemented error. */
    METHOD_ERROR,      /* Couldn't parse request method (GET, POST). */
    HOST_ERROR,        /* Couldn't parse host name. */
    SCHEME_ERROR,      /* Couldn't parse scheme (http, https). */
    URI_ERROR,         /* Couldn't parse URI. */
    PORT_ERROR,        /* Couldn't parse port number. */
    PATH_ERROR,        /* Couldn't parse path. */
    HTTP_VERSION_ERROR /* Couldn't parse http version (1.1, 1.0). */
} error_t;

/**************** GLOBALS ****************/
cfg_t g_cfg;
cache_t *g_cache;

/**************** Attempt at a FIFO queue for readers/writers ****************/
typedef struct TOK {
    bool is_reader;
    struct TOK *next;
} rw_token_t;

typedef struct {
    pthread_mutex_t mutex;
    int reading_count;
    int writing_count;
    rw_token_t *head;
    rw_token_t *tail;
} rw_queue_t;

void rw_queue_init(rw_queue_t *q) {
    pthread_mutex_init(&q->mutex, NULL);
    q->reading_count = q->writing_count = 0;
    q->head = q->tail = NULL;
}

static void enqueue(rw_queue_t *q, rw_token_t *t) {
    if (q->tail == NULL)
        q->tail = q->head = t;
    else {
        q->tail->next = t;
        q->tail = t;
    }
    t->next = NULL;
}

static void dequeue(rw_queue_t *q) {
    rw_token_t *t = q->head;
    if (t == NULL)
        return;
    q->head = t->next;
    if (q->head == NULL)
        q->tail = NULL;
}

void rw_queue_request_read(rw_queue_t *q, rw_token_t *t) {
    pthread_mutex_lock(&q->mutex);
    if (q->head == NULL && q->writing_count == 0)
        q->reading_count++;
    else {
        t->is_reader = true;
        enqueue(q, t);
    }
    pthread_mutex_unlock(&q->mutex);
}

void rw_queue_request_write(rw_queue_t *q, rw_token_t *t) {
    pthread_mutex_lock(&q->mutex);
    if (q->head == NULL && q->writing_count == 0 && q->reading_count == 0)
        q->writing_count++;
    else {
        t->is_reader = false;
        enqueue(q, t);
    }
    pthread_mutex_unlock(&q->mutex);
}

void rw_queue_release(rw_queue_t *q) {
    rw_token_t *t;
    pthread_mutex_lock(&q->mutex);

    if (q->writing_count > 0)
        q->writing_count--;
    else
        q->reading_count--;

    t = q->head;
    if (t == NULL) {
        pthread_mutex_unlock(&q->mutex);
        return;
    }
    bool want_to_read = t->is_reader;
    if (!want_to_read && q->reading_count == 0) {
        q->writing_count++;
        dequeue(q);
    } else {
        while (want_to_read) {
            q->reading_count++;
            dequeue(q);
            t = q->head;
            want_to_read = t && t->is_reader;
        }
    }
    pthread_mutex_unlock(&q->mutex);
}

rw_queue_t g_rw_queue;

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1";

/**************** FUNCTIONS ****************/
/**
 * @brief Parser commandline arguments / opts.
 *
 * Options are:
 * - `-v` verbose mode.
 *
 * @param[out]  cfg   Configuration to determine runtime behavior.
 * @param[in]   argc  Number of commandline arguments passed.
 * @param[in]   argv  String values of passed arguments.
 */
void parse_args(cfg_t *cfg, const int argc, char *const argv[]) {
    char opt;
    const char *usage_str = "Usage: %s [port] [-v verbose]\n";
    if (argc > 3) {
        fprintf(stderr, usage_str, argv[0]);
        exit(EXIT_FAILURE);
    }

    // Get opt arguments.
    cfg->verbose = false;
    while ((opt = getopt(argc, argv, "v")) != EOF) {
        switch (opt) {
        case 'v':
            cfg->verbose = true;
            break;

        // Misspecified argument(s).
        default:
            fprintf(stderr, usage_str, argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Get non-opt arguments.
    if (optind < argc)
        while (optind < argc)
            cfg->port = argv[optind++];
}

/*
 * Copied from tiny.c
 * clienterror - returns an error message to the client.
 */
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Proxy Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>Proxy</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

/**
 * @brief Extract request data from the parser and fill the request struct with
 * said data.
 *
 * @param[out]  request    Struct to fill with request data. All fields will be
 *                         set to NULL. If any field remains NULL after calling
 *                         this function, an error has occurred.
 * @param[in]   parser     A parser that has ingested a client request.
 * @param[in]   client_fd  Open file descriptor for the client process.
 *
 * @return OK (i.e. 0) if extraction of request from the parser was successful.
 * @return Non-zero error code otherwise (cf. error_t enum).
 */
static error_t retrieve_request(request_t *request, parser_t *parser,
                                int client_fd) {
    request->method = request->host = request->scheme = request->uri =
        request->port = request->path = request->http_version = NULL;
    const char *parse_val;

    // Method
    if (parser_retrieve(parser, METHOD, &parse_val) < 0)
        return METHOD_ERROR;
    if (strcmp(parse_val, "GET") != 0) {
        clienterror(client_fd, "501", "Not Implemented",
                    "Proxy does not implement POST");
        return ERROR_501;
    }
    request->method = parse_val;

    // Host
    if (parser_retrieve(parser, HOST, &parse_val) < 0)
        return HOST_ERROR;
    request->host = parse_val;

    // Scheme
    if (parser_retrieve(parser, SCHEME, &parse_val) < 0)
        return SCHEME_ERROR;
    if (strcmp(parse_val, "http") != 0) {
        clienterror(client_fd, "501", "Not Implemented",
                    "Proxy does not implement https.");
        return ERROR_501;
    }
    request->scheme = parse_val;

    // URI
    if (parser_retrieve(parser, URI, &parse_val) < 0)
        return URI_ERROR;
    request->uri = parse_val;

    // Port
    if (parser_retrieve(parser, PORT, &parse_val) < 0)
        return PORT_ERROR;
    request->port = parse_val;

    // Path
    if (parser_retrieve(parser, PATH, &parse_val) < 0)
        return PATH_ERROR;
    request->path = parse_val;

    // HTTP version
    if (parser_retrieve(parser, HTTP_VERSION, &parse_val) < 0)
        return HTTP_VERSION_ERROR;
    request->http_version = parse_val;

    return OK;
}

/**
 * @brief Obtain and parse a client request. The request itself is stored in a
 * special request struct, whereas the headers are stored in the parser itself
 * (they can be accessed via `parser_retrieve_next_header`).
 *
 * @param[in]   client_fd  Open file descriptor for client.
 * @param[out]  parser     A parser_t struct that will be filled with header
 *                         data.
 * @param[out]  request    A struct that will be filled with metadata about the
 *                         request.
 *
 * @return OK (i.e. 0) if parsing was successful.
 * @return Non-zero error code associated with the parsing error (cf. the
 *         error_t enum).
 */
static error_t get_client_request(int client_fd, parser_t *parser,
                                  request_t *request) {
    char buf[PARSER_MAXLINE];
    ssize_t res;
    int parse_state;
    rio_t rio;
    rio_readinitb(&rio, client_fd);

    while ((res = rio_readlineb(&rio, buf, sizeof(buf))) > 0) {
        parse_state = parser_parse_line(parser, buf);
        switch (parse_state) {
        case REQUEST: {
            error_t status = retrieve_request(request, parser, client_fd);
            if (status != OK)
                return status;
            break;
        }

        case HEADER:
            break;
        case ERROR:
            return PARSER_ERROR;
        }

        // Halt at HTTP end of request line.
        if (strcmp(buf, "\r\n") == 0)
            break;
    }

    // In case we broke out of the while loop right away due to a parser error.
    if (res < 0)
        return PARSER_ERROR;
    return OK;
}

/**
 * @brief  Take a parsed client request and assemble it into a string that can
 * be sent to the target server.
 *
 * @note  There are some headers / options that are always chosen regardless of
 * what we recieved from the client. All other client headers are preserved.
 * - HTTP/1.0 version is always used for the request.
 * - Fixed headers for Connection, Proxy-Connection, and User-Agent are always
 *   sent.
 *
 * @param[out]  request_str      Pointer to a buffer of at least
 *                               `request_max_len` bytes.
 * @param[in]   request_max_len  Maximum number of bytes that may be written to
 *                               `request_str`.
 * @param[out]  parser           Parser that has successfully ingested a client
 *                               request.
 * @param[in]   request          Values parsed by parser. This is obtained from
 *                               `get_client_request`.
 *
 * @return  EXIT_SUCCESS (i.e. 0) if the request was successfully assembled.
 * @return  res  (< 0) Error return value of snprintf.
 * @return  res  (> 0) Number of bytes that would have been written if the write
 *               exceeded the number of allowed bytes `request_max_len`.
 */
static ssize_t assemble_request_str(char *request_str, size_t request_max_len,
                                    parser_t *parser,
                                    const request_t *request) {
    header_t *header;
    ssize_t length = 0;

    // Assemble request and reserved headers.
    ssize_t res = snprintf(request_str, request_max_len,
                           "%s %s HTTP/1.0\r\n"
                           "Connection: close\r\n"
                           "Proxy-Connection: close\r\n"
                           "User-Agent: %s\r\n",
                           request->method, request->uri, header_user_agent);
    length += res;
    if (res < 0 || ((size_t)res >= request_max_len - length))
        return (res < 0) ? res : res + length;

    // Assemble extra client headers.
    while ((header = parser_retrieve_next_header(parser))) {
        // Skip reserved headers
        if (strcmp(header->name, "Connection") == 0)
            continue;
        if (strcmp(header->name, "Proxy-Connection") == 0)
            continue;
        if (strcmp(header->name, "User-Agent") == 0)
            continue;

        res = snprintf(request_str + length, request_max_len - length,
                       "%s: %s\r\n", header->name, header->value);

        // Assume that we want to go to the next header if this one couldn't
        // be written.
        if (res < 0 || ((size_t)res >= request_max_len - length))
            return (res < 0) ? res : res + length;
        length += res;
    }

    // Add request ending marker.
    res = snprintf(request_str + length, request_max_len - length, "\r\n");
    if (res < 0 || ((size_t)res >= request_max_len - length))
        return (res < 0) ? res : res + length;
    return EXIT_SUCCESS;
}

/**
 * @todo
 */
static inline block_t *get_cached_response(request_t *request) {
    if (request->uri == NULL)
        return NULL;
    return cache_find(g_cache, request->uri, strlen(request->uri) + 1);
}

/**
 * @todo
 */
static inline void request_init(request_t *request) {
    request->method = request->host = request->scheme = request->uri =
        request->port = request->path = request->http_version = NULL;
}

/**
 * @todo
 */
static inline bool is_request_filled(request_t *request) {
    return (request->method != NULL) && (request->host != NULL) &&
           (request->scheme != NULL) && (request->uri != NULL) &&
           (request->port != NULL) && (request->path != NULL) &&
           (request->http_version != NULL);
}

/**
 * @brief Function that encompasses the runtime of each thread that is spawned
 * to manage relaying client requests and server responses to said requests.
 * Each thread detaches itself, thereby causing cleanup to happen automatically
 * upon exiting.
 *
 * @shared  g_cfg  Constant user configuration for runtime of entire proxy.
 *
 * @param  vargp  Passed argument from the main thread. This will just be the
 * client file descriptor value stored in a pointer.
 */
static void *thread_handle_relay(void *vargp) {
    // Detach thread so that it'll clean up after itself after exiting.
    pthread_detach(pthread_self());

    rw_token_t reader_tok;
    rw_token_t writer_tok;

    size_t client_fd = (size_t)vargp;

    // Retrieve HTTP request from the client.
    // Assume that request is sent in one chunk.
    parser_t *parser = parser_new();
    request_t request;
    request_init(&request);
    if (get_client_request(client_fd, parser, &request) != OK) {
        if (g_cfg.verbose)
            perror("parser");
        close(client_fd);
        parser_free(parser);
        pthread_exit(NULL);
    }

    // If the client has prematurely closed the socket, we'll end up with an
    // unitialized request. We should immediately close the connection and
    // exit the thread.
    if (!is_request_filled(&request)) {
        close(client_fd);
        parser_free(parser);
        pthread_exit(NULL);
    }

    // Check for cached server response.
    rw_queue_request_read(&g_rw_queue, &reader_tok);
    block_t *response = get_cached_response(&request);
    if (response) {
        if (rio_writen(client_fd, response->value, response->size) < 0) {
            if (g_cfg.verbose)
                perror("rio_writen client");
        }
        rw_queue_release(&g_rw_queue);

        close(client_fd);
        parser_free(parser);
        pthread_exit(NULL);
    }
    rw_queue_release(&g_rw_queue);

    // Assemble HTTP request to server.
    char request_str[MAXLINE];
    if (assemble_request_str(request_str, MAXLINE, parser, &request)) {
        if (g_cfg.verbose)
            perror("sprintf assemble");
        close(client_fd);
        parser_free(parser);
        pthread_exit(NULL);
    }

    // Establish connection to server.
    int server_fd = open_clientfd(request.host, request.port);
    if (server_fd < 0) {
        if (g_cfg.verbose)
            fprintf(stderr, "[PROXY] Failed to connect to server %s:%s\n",
                    request.host, request.port);
        close(client_fd);
        parser_free(parser);
        pthread_exit(NULL);
    }

    // Relay assembled request to server.
    // Assume that the request can be sent in one chunk.
    if (rio_writen(server_fd, request_str, strlen(request_str)) < 0) {
        if (g_cfg.verbose)
            perror("rio_writen server");
        close(client_fd);
        close(server_fd);
        parser_free(parser);
        pthread_exit(NULL);
    }

    // Read response(s) from server and relay to client.
    // We account for the server splitting its response into multiple
    // chunks.
    rio_t rio_server;
    rio_readinitb(&rio_server, server_fd);
    char buf_accum[MAX_OBJECT_SIZE];
    ssize_t rsize;
    size_t offset = 0;
    bool cache_buf = true;
    while ((rsize = rio_readnb(&rio_server, &buf_accum[offset],
                               MAX_OBJECT_SIZE - offset)) > 0) {
        // Relay response chunk to client.
        if (rio_writen(client_fd, &buf_accum[offset], rsize) < 0) {
            if (g_cfg.verbose)
                perror("rio_writen client");
            break;
        }

        // Accumulate server response chunks for caching.
        if (offset + rsize >= MAX_OBJECT_SIZE)
            cache_buf = false;
        offset += rsize;
        offset %= MAX_OBJECT_SIZE - 1;
    }

    // Cache the response if it isn't too large.
    // This will not re-insert duplicates.
    if (cache_buf) {
        rw_queue_request_write(&g_rw_queue, &writer_tok);
        cache_insert(g_cache, request.uri, strlen(request.uri) + 1, buf_accum,
                     offset);
        rw_queue_release(&g_rw_queue);
    }

    // Cleanup thread resources.
    close(client_fd);
    close(server_fd);
    parser_free(parser);

    // We must allow other threads to continue execution when exiting this one.
    pthread_exit(NULL);
}

/**
 * @brief Trivial handler for not exiting on SIGPIPE signals.
 */
void sigpipe_handler(int sig) {}

/**
 * @brief Driver for the sequential network proxy.
 *
 * 1. Listen for connections on specified port.
 * 2. On connection, retrieve and process client request.
 *   - Modify client request according to spec: HTTP/1.0 and a couple of
 *     reserved headers.
 * 3. Relay request to target server.
 *   - Assume single chunk request.
 * 4. Relay server response to client.
 *   - Relay all chunks sent by server in the case of more than one response.
 */
int main(int argc, char **argv) {
    parse_args(&g_cfg, argc, argv);
    if (g_cfg.verbose)
        printf("header: %s"
               "port:   %s\n",
               header_user_agent, g_cfg.port);

    g_cache = cache_init(MAX_CACHE_SIZE);
    rw_queue_init(&g_rw_queue);

    // Install signal handlers.
    // When sockets disconnect, the kernel may send SIGPIPE to this process --
    // we need to make sure we don't exit as a result.
    Signal(SIGPIPE, sigpipe_handler);

    // Start listening to specified port.
    int listenfd = open_listenfd(g_cfg.port);
    if (listenfd < 0) {
        perror("open_listenfd");
        exit(EXIT_FAILURE);
    }

    while (1) {
        client_info_t client;
        client.addrlen = sizeof(client.addr);

        // Wait until client connects.
        // @note The client file will become the responsibility of the thread
        // spawned to handle it.
        //       This means that we will not close it here in the main thread.
        client.connfd = accept(listenfd, (SA *)&client.addr, &client.addrlen);
        if (client.connfd < 0) {
            if (g_cfg.verbose)
                perror("accept");
            continue;
        }

        // Retrieve connected client info.
        // Not doing this in a separate thread because I don't want to deal with
        // locking on the `client` struct.
        int res = getnameinfo((SA *)&client.addr, client.addrlen, client.host,
                              sizeof(client.host), client.serv,
                              sizeof(client.serv), 0);
        if (res) {
            if (g_cfg.verbose)
                perror("getnameinfo client");
            close(client.connfd);
            continue;
        }

        // Launch a thread to handle relay of client request and server
        // response.
        size_t client_fd = client.connfd;
        pthread_t thread_id;
        pthread_create(&thread_id, NULL, thread_handle_relay,
                       (void *)client_fd);
    }

    // Final resource cleanup.
    close(listenfd);
    cache_free(g_cache);
    return 0;
}
