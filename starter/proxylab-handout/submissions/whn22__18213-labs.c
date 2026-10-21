/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

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
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "User-Agent: Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1\r\n";

typedef struct sockaddr SA;
static queue *cache_queue;

void *thread(void *vargp);
void serv(int connfd);
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void request_header(char *header_http, char *path, rio_t *rio);

// copy from tiny.c
int main(int argc, char **argv) {
    int listenfd;
    // check command args
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int *connfd;
    signal(SIGPIPE, SIG_IGN);

    cache_queue = create_cache();
    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }

    pthread_t tid;
    while (1) {
        struct sockaddr_storage clientaddr;
        socklen_t clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (*connfd < 0) {
            perror("accept");
            continue;
        }

        pthread_create(&tid, NULL, thread, connfd);
    }

    free_queue(cache_queue);
    return 0;
}

// TODO: thread
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    Free(vargp);
    serv(connfd);
    close(connfd);
    return NULL;
}

// copy from tiny.c
void serv(int connfd) {
    char buf[MAXLINE];
    char host[MAXLINE], path[MAXLINE], port[MAXLINE];
    char header_http[MAXLINE];

    rio_t rio, rio_sever;
    rio_readinitb(&rio, connfd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        return;
    }

    /* Parse the request line and check if it's well-formed */
    char method[MAXLINE];
    char uri[MAXLINE];
    char version;

    /* sscanf must parse exactly 3 things for request line to be well-formed */
    /* version must be either HTTP/1.0 or HTTP/1.1 */
    if (sscanf(buf, "%s %s HTTP/1.%c", method, uri, &version) != 3 ||
        (version != '0' && version != '1')) {
        clienterror(connfd, "400", "Bad Request",
                    "Proxy received a malformed request");
        return;
    }

    if (strcmp(method, "GET")) {
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    parse_uri(uri, host, path, port);
    request_header(header_http, path, &rio);

    size_t response_size = 0;
    char *response;
    response = read_from_cache(cache_queue, uri, &response_size);
    // hit
    if (response != NULL) {
        rio_writen(connfd, response, response_size);
        Free(response);
        return;
    }

    // miss
    int client_fd = open_clientfd(host, port);
    if (client_fd < 0) {
        printf("connection failed\n");
        return;
    }

    rio_readinitb(&rio_sever, client_fd);
    rio_writen(client_fd, header_http, strlen(header_http));

    size_t n;
    bool overflow = false;
    size_t count = 0;
    char data[MAX_OBJECT_SIZE];
    char *ptr;

    while ((n = rio_readnb(&rio_sever, buf, MAXLINE)) != 0) {
        size_t sum = count + n;
        if (sum > MAX_OBJECT_SIZE)
            overflow = true;
        else {
            ptr = data + count;
            memcpy(ptr, buf, n);
            count += n;
        }
        rio_writen(connfd, buf, n);
    }

    if (overflow) {
        printf("exceed MAX_OBJECT_SIZE");
    } else {
        in_cache(cache_queue, uri, data, count);
    }

    close(client_fd);
}

void request_header(char *header_http, char *path, rio_t *rio) {
    char request_line[MAXLINE];
    char host_header[MAXLINE];
    char str[MAXLINE];
    char buf[MAXLINE];

    sprintf(request_line, "GET %s HTTP/1.0\r\n", path);

    while (rio_readlineb(rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0)
            break;

        if (!strncasecmp(buf, "Host", strlen("Host"))) {
            strncpy(host_header, buf, strlen(buf));
        } else if (!strncasecmp(buf, "User-Agent", strlen("User-Agent")) &&
                   !strncasecmp(buf, "Connection", strlen("Connection")) &&
                   !strncasecmp(buf, "Proxy-Connection",
                                strlen("Proxy-Connection"))) {
            continue;
        } else {
            strcat(str, buf);
        }
    }

    sprintf(header_http, "%s%s%s%s%s%s%s", request_line, host_header,
            header_user_agent, "Connection: close\r\n",
            "Proxy-Connection: close\r\n", str, "\r\n");

    return;
}

void parse_uri(char *uri, char *host, char *path, char *port) {
    char *host_str = strstr(uri, "//");
    host_str = host_str + 2;
    char *path_str = strstr(host_str, "/");
    char *port_str = strstr(host_str, ":");

    strcpy(path, path_str);
    int hostLength = path_str - host_str;
    int portLength = path_str - port_str - 1;

    // default port
    if (port_str == NULL) {
        strcpy(port, "80");
        strncpy(host, host_str, hostLength);
    } else {
        strncpy(port, port_str + 1, portLength);
        strncpy(host, host_str, hostLength - portLength - 1);
    }
}

/*
 * copy from tiny.c
 * clienterror - returns an error message to the client
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