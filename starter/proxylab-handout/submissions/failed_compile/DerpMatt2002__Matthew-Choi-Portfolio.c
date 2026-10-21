

/* Some useful includes to help you get started */

#include "cache.h"
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
#include <http_parser.h>
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

#define HOSTLEN 256
#define SERVLEN 8

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
/*static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20220411 Firefox/63.0.1";*/

static cache_t *cache;
pthread_mutex_t lock;

typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;

typedef struct sockaddr SA;

void cstringcpy(char *src, char *dest) {
    while (*src) {
        *(dest++) = *(src++);
    }
    *dest = '\0';
}

void *thread(void *fd) {
    // Dereference and free then detaching fd
    int connectfd = *(int *)fd;
    // free(fd);
    pthread_detach(pthread_self());

    // Reading request
    rio_t rio;
    rio_readinitb(&rio, connectfd);

    char buf[MAXLINE];

    if (rio_readlineb(&rio, buf, sizeof(buf)) <= 0) {
        close(connectfd);
        return NULL;
    }

    // Parsing through the recieved request
    parser_t *parse = parser_new();
    const char *host, *port, *method, *path, *url;

    parser_parse_line(parse, buf);
    parser_retrieve(parse, HOST, &host);
    parser_retrieve(parse, PORT, &port);
    parser_retrieve(parse, METHOD, &method);
    parser_retrieve(parse, PATH, &path);
    parser_retrieve(parse, URI, &url);

    // Checking cache before sending request
    char message[MAXLINE];
    char server_request[MAXBUF];

    // Making a request when the url isn't in the cache
    sprintf(server_request, "%s %s HTTP/1.0\r\n", method, path);
    char request_other[MAXBUF];
    int length;

    while ((length = rio_readlineb(&rio, request_other,
                                   sizeof(request_other))) > 0) {
        printf("%d: %s\n", length, request_other);

        if (strstr(request_other, ":") == NULL)
            break;

        strcat(server_request, request_other);
    }

    strcat(server_request, "\r\n");

    // Sending request
    int serverfd;
    serverfd = open_clientfd(host, port);

    rio_writen(serverfd, server_request, strlen(server_request));

    int size;

    // Return the message to the client
    while ((size = rio_readn(serverfd, message, MAXLINE)) > 0) {
        rio_writen(connectfd, message, size);
    }
    // sio_printf("message = %s\n",message);
    // sio_printf("size = %d\n",sizeof(message));
    //  Adding to cache
    //  store_line(cache, url, message, size);

    // Closing serverfd
    close(serverfd);

    // Closing and freeing the rest

    close(connectfd);
    parser_free(parse);

    // Removing lock now that it is done
    rem_line(cache, url);
    // return NULL;
}

void sig_handler(int sig) {
    printf("Received SIGPIPE signal\n");
}

int main(int argc, char **argv) {
    pthread_mutex_init(&lock, NULL);
    Signal(SIGPIPE, sig_handler);
    pthread_t td;
    // Initializing a new cache
    cache = malloc(sizeof(cache_t));

    // EDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDITEDIT
    // Check for valid argument format
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // Open listening file descriptor
    int listenfd = open_listenfd(argv[1]);

    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen to port: %s\n", argv[1]);
        exit(1);
    }

    for (;;) {
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
            close(client->connfd);
            continue;
        }

        // Connection is established; serve client
        int res = getnameinfo((SA *)&client->addr, client->addrlen,
                              client->host, sizeof(client->host), client->serv,
                              sizeof(client->serv), 0);
        if (res != 0) {
            fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
        }

        // Creating thread
        int *fd = Malloc(sizeof(int));
        *fd = client->connfd;

        pthread_create(&td, NULL, thread, fd);
    }

    // printf("%s", header_user_agent); Huh.

    return 0;
}
