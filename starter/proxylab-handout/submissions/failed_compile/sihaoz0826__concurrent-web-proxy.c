/**
 * @file proxy_server.c
 * @brief Proxy Server Implementation for HTTP requests.
 *
 * This file contains the implementation of a multithreaded proxy server that
 * handles HTTP requests and responses. The proxy intercepts HTTP client
 * requests, processes them, and forwards them to the appropriate web server.
 * Upon receiving a response from the web server, it forwards the response back
 * to the client. This proxy server supports basic HTTP GET requests and is
 * capable of handling multiple client connections concurrently using POSIX
 * threads.
 *
 * Features:
 * - Connection handling with POSIX threads to manage multiple clients
 * simultaneously.
 * - HTTP request parsing and response forwarding.
 * - Error handling to manage common HTTP errors and client-server interaction
 * issues.
 * - SIGPIPE signal handling to gracefully handle attempts to write to closed
 * socket connections.
 *
 * The server is initialized on a specified port provided via command line
 * arguments. It continually listens for incoming connections, accepts them, and
 * spawns a new thread to handle each connection independently. This allows the
 * server to manage multiple client requests in parallel, improving throughput
 * and performance in multi-client scenarios.
 *
 * Usage:
 * To run the proxy server, compile with appropriate flags to include necessary
 * libraries (like pthreads and network libraries), and execute with the command
 * line argument specifying the port number on which the server should listen.
 */

#include "csapp.h"

#include <assert.h>
#include <ctype.h>
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

#include <http_parser.h>
#include <string.h>

/* Typedef for convenience */
typedef struct sockaddr SA;

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
#define HOSTLEN 256
#define SERVLEN 8

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20230411 Firefox/63.0.1\r\n";
static const char *header_connection = "Connection: close\r\n";
static const char *header_proxy = "Proxy-Connection: close\r\n";

/* Information about a connected client. */
typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;

/* URI parsing results. */
typedef enum { PARSE_ERROR, PARSE_STATIC, PARSE_DYNAMIC } parse_result;

/**
 * @brief Handles the SIGPIPE signal.
 *
 * @param[in] signal The signal number.
 *
 * This function is a signal handler for the SIGPIPE signal.
 * The SIGPIPE signal is sent to a process when it attempts to write to a pipe
 * without a process connected to the other end. In this case, the function
 * simply returns and does nothing. This can prevent the process from being
 * terminated if it doesn't check the return value of a write to a pipe.
 */
static void sigpipe_handler(int signal) {
    return;
}

/**
 * @brief Sends an HTTP error response to the client.
 *
 * @param[in] fd The file descriptor of the client.
 * @param[in] cause The cause of the error.
 * @param[in] errnum The HTTP error number.
 * @param[in] shortmsg The short error message.
 * @param[in] longmsg The long error message.
 *
 * Adaped from CS.APP!!!
 *
 * This function is called when an error occurs while handling a client request.
 * It first builds the HTTP response body with the error details.
 * It then prints the HTTP response header and the response body to the client.
 */
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    // Build the HTTP response body
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Tiny Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>The Tiny Web server</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    // Build the HTTP response headers
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    // Write the headers
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    // Write the body
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

/**
 * @brief Fills in the HTTP header for a client request.
 *
 * @param[in] parser The parser_t structure for the client.
 * @param[out] header The buffer to store the HTTP header.
 * @param[in] method The HTTP method of the client request.
 * @param[in] host The host of the client.
 * @param[in] scheme The scheme of the client request.
 * @param[in] uri The URI of the client request.
 * @param[in] port The port of the client.
 * @param[in] path The path of the client request.
 * @param[in] http_version The HTTP version of the client request.
 *
 * This function is called to fill in the HTTP header for a client request.
 * It first initializes the necessary variables and buffers.
 * It then enters a loop where it reads each line of the client request.
 * If the line is empty, it breaks the loop.
 * If the line contains a "Host:", "Connection:", "Proxy-Connection:", or
 * "User-Agent:" field, it skips the line. Otherwise, it appends the line to the
 * HTTP header. After reading all the lines, it appends the "Host:",
 * "User-Agent:", "Connection:", and "Proxy-Connection:" fields to the HTTP
 * header. Finally, it adds a "\r\n" at the end of the HTTP header.
 */
void fill_header(parser_t *parser, char *header, const char *method,
                 const char *host, const char *scheme, const char *uri,
                 const char *port, const char *path, const char *http_version) {

    // method + path + http_version
    sprintf(header, "%s%s %s HTTP/%s\r\n", header, method, path, http_version);

    // host + port
    sprintf(header, "%sHost: %s:%s\r\n", header, host, port);

    // user agent
    sprintf(header, "%sUser-Agent: %s", header, header_user_agent);

    // connection
    sprintf(header, "%s%s", header, header_connection);

    // proxy connection
    sprintf(header, "%s%s", header, header_proxy);

    // For the rest, loop through the parser to get the stuff
    header_t *header_iterator;
    while ((header_iterator = parser_retrieve_next_header(parser)) != NULL) {
        // If header_iterator's name is
        // Host, Connection, Proxy-connection, User-Agent
        // Continue
        if (strcmp(header_iterator->name, "Host") == 0 ||
            strcmp(header_iterator->name, "Connection") == 0 ||
            strcmp(header_iterator->name, "Proxy-connection") == 0 ||
            strcmp(header_iterator->name, "User-agent") == 0) {
            continue;
        }

        // Otherwise, append the header (the extra info)
        sprintf(header, "%s%s: %s\r\n", header, header_iterator->name,
                header_iterator->value);
    }

    // add \r\n at the end
    sprintf(header, "%s\r\n", header);

    return;
}

/**
 * @brief Handles a client request.
 *
 * @param[in] client_fd The file descriptor of the client.
 *
 * Adapted from CS.APP!!!
 *
 * This function is called to handle a client request.
 * It first initializes the necessary variables and buffers.
 * It then reads the client request and parses it.
 * If the request method is not GET or the HTTP version is not 1.0 or 1.1, it
 * sends an error message to the client and returns. It then parses the URI from
 * the request and fills in the HTTP header. It opens a connection to the server
 * specified by the URI and sends the HTTP header to the server. It then reads
 * the server response and sends it to the client. After handling the request,
 * it closes the connection to the server.
 */
void doit(int client_connfd) {

    // Initialize the client rio
    rio_t rio_client;
    rio_readinitb(&rio_client, client_connfd);

    // Initialize the parser
    parser_t *parser = parser_new();

    // Initialize the buf used to put things in parser
    char buf[MAXLINE];

    // cnt is used to differentiate between the first line and the rest
    int cnt = 0;

    // Read the stuff into the parser
    while (true) {

        // Error case
        if (rio_readlineb(&rio_client, buf, sizeof(buf)) <= 0) {
            perror("readline");
            break;
        }

        // When reach the end, just break
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }

        // Read the thing from buf into the parser
        parser_state parse_state = parser_parse_line(parser, buf);

        // Error handling, the first line should be type REQUEST
        // So we just skipped the first line
        if (parse_state != HEADER && cnt > 0) {
            clienterror(client_connfd, "400", "Bad Request",
                        "Could not parse request headers");
            break;
        }

        // Keep track of which line it is
        cnt++;
    }

    // Now parser store all the things
    // Get individual things out of the parser (parser_retrieve)
    // METHOD,      < HTTP request method, e.g. GET or POST
    // HOST,        < a network host, e.g. www.example.com
    // SCHEME,      < scheme to connect over, e.g. http or https
    // URI,         < the entire URI
    // PORT,        < The port to connect on, by default 80
    // PATH,        < The path to find a resource, e.g. index.html
    // HTTP_VERSION < The HTTP version without the HTTP/, e.g. 1.0 or 1.1
    const char *method, *host, *scheme, *uri, *port, *path, *http_version;
    parser_retrieve(parser, METHOD, &method);
    parser_retrieve(parser, HOST, &host);
    parser_retrieve(parser, SCHEME, &scheme);
    parser_retrieve(parser, URI, &uri);
    parser_retrieve(parser, PORT, &port);
    parser_retrieve(parser, PATH, &path);
    parser_retrieve(parser, HTTP_VERSION, &http_version);

    // Build the http header
    char header[MAXLINE];
    memset(&header, 0, MAXLINE);
    fill_header(parser, header, method, host, scheme, uri, port, path,
                http_version);

    // Initialize server_fd
    int server_fd;
    server_fd = open_clientfd(host, port);

    // Check if the server is valid
    if (server_fd < 0) {
        perror("open_serverfd");
        return;
    }

    // Initialize the server rio
    rio_t rio_server;

    // Write the header to the server
    rio_writen(server_fd, header, strlen(header));

    // Initialize the server rio
    rio_readinitb(&rio_server, server_fd);

    // Create int n for number of bytes read
    int n;

    // Write the things in a loop
    // Use readnb because it can be binary file too (no \r\n)!!!
    while ((n = rio_readnb(&rio_server, buf, MAXLINE)) != 0) {
        // Write the buffer to the client
        rio_writen(client_connfd, buf, n);
    }

    // Free the parser
    parser_free(parser);

    // Close the server
    close(server_fd);
}

/**
 * @brief Handles a client request in a separate thread.
 *
 * @param[in] vargp The client connection file descriptor.
 *
 * This function is called to handle a client request in a separate thread.
 * It first retrieves the client connection file descriptor from the input
 * parameter. It then detaches the thread to allow it to run independently. When
 * the thread finishes, its resources are automatically released back to the
 * system without the need for another thread to join with it. It then calls the
 * doit function to handle the client request. After handling the client
 * request, it closes the client connection. It then returns NULL to indicate
 * the end of the thread.
 */
void *thread(void *vargp) {

    // Get the client connection file descriptor
    int client_connfd = *(int *)vargp;

    // Run in detach mode
    pthread_detach(pthread_self());

    // Handle the client request
    doit(client_connfd);

    // Close the client connection
    close(client_connfd);

    return NULL;
}

/**
 * @brief Main entry point of the proxy server.
 *
 * @param[in] argc The number of command-line arguments.
 * @param[in] argv The command-line arguments.
 *
 * Adapted from CS.APP!!!
 *
 * This function is the main entry point of the proxy server.
 * It first checks if the number of command-line arguments is correct. If not,
 * it prints an error message and exits. It then opens a listening descriptor on
 * the port specified by the command-line argument. It enters an infinite loop
 * where it accepts connections from clients. For each connection, it gets the
 * hostname and port of the client and prints them. It then calls the `doit`
 * function to handle the client request sequentially. After handling the
 * request, it closes the connection. The function returns 0 to indicate
 * successful termination.
 */

int main(int argc, char **argv) {

    // Initialize the listening file descriptor and thread id
    int listenfd;
    pthread_t tid;

    // Check command line args
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // We don't want to terminate the process when writing to a pipe with no
    // reader
    Signal(SIGPIPE, sigpipe_handler);

    // Open listening file descriptor
    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }

    while (1) {
        // Allocate space on the stack for client info
        client_info client_data;
        client_info *client = &client_data;

        // Initialize the length of the address
        client->addrlen = sizeof(client->addr);

        // accept() will block until a client connects to the port
        client->connfd =
            accept(listenfd, (SA *)&client->addr, &client->addrlen);
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        // Create a thing to pass in to avoid concurrency issues
        int *arg = calloc(sizeof(client->connfd), 1);
        *arg = client->connfd;

        // Concurrent handling
        pthread_create(&tid, NULL, thread, (void *)(arg));
    }

    return 0;
}