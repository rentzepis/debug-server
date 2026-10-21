/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 * name: Yu-Chi Pai
 * andrewID: ypai
 */

/* Some useful includes to help you get started */

#include "cache.h"
#include "csapp.h"
#include "http_parser.h"

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
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/* Typedef for convenience */
typedef struct sockaddr SA;

/*
 * String to use for the User-Agent header.
 */
static const char *header_user_agent = "User-Agent: Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20220411 Firefox/63.0.1\r\n";

static const char *header_connection = "Connection: close\r\n";
static const char *header_proxy_connection = "Proxy-Connection: close\r\n";

/*
 * clienterror - returns an error message to the client
 * from tiny.c
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
                       "<hr /><em>The Proxy Web server</em>\r\n"
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

/*
 * serve - handle one HTTP request/response transaction
 * modify from the same function in tiny.c
 */
static void serve(int connfd) {
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    rio_readinitb(&rio, connfd);
    /* 1. Read request line */
    if (rio_readlineb(&rio, buf, sizeof(buf)) <= 0) {
        return;
    }
    // printf("%s", buf);

    parser_t *parser = parser_new();
    if (!parser) {
        return;
    }

    /* 2. Parse request line and check if it's well-formed */
    parser_state state = parser_parse_line(parser, buf);
    if (state != REQUEST) {
        clienterror(connfd, "400", "Bad Request",
                    "Proxy could not parse the request line");
        parser_free(parser);
        return;
    }

    const char *method;
    const char *uri;
    const char *http_version;

    // parse exactly 3 things for request line to be well-formed
    if (parser_retrieve(parser, METHOD, &method) < 0 ||
        parser_retrieve(parser, URI, &uri) < 0 ||
        parser_retrieve(parser, HTTP_VERSION, &http_version) < 0) {

        clienterror(connfd, "400", "Bad Request",
                    "Proxy could not parse the request line");
        parser_free(parser);
        return;
    }

    // Check that the method is GET (METHOD could be POST)
    if (strcmp(method, "GET")) {
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy  does not implement this method");
        parser_free(parser);
        return;
    }

    /* Support http only (no https) */
    const char *scheme;
    if (parser_retrieve(parser, SCHEME, &scheme) == 0 && scheme &&
        strcasecmp(scheme, "http")) {
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy does not support this protocol");
        parser_free(parser);
        return;
    }

    const char *host;
    const char *port;
    const char *path;

    if (parser_retrieve(parser, HOST, &host) < 0) {
        clienterror(connfd, "400", "Bad Request", "Proxy could not parse host");
        parser_free(parser);
        return;
    }

    if (parser_retrieve(parser, PORT, &port) < 0) {
        clienterror(connfd, "400", "Bad Request", "Proxy could not parse post");
        parser_free(parser);
        return;
    }

    if (parser_retrieve(parser, PATH, &path) < 0) {
        clienterror(connfd, "400", "Bad Request", "Proxy could not parse path");
        parser_free(parser);
        return;
    }

    /* 3. read, parse, and buffer request header*/
    // iv. create the request header
    // v.  grab all client headers and store them in a buffer
    bool has_own_host_header = false;
    char header_host[MAXLINE] = {0};
    char remaining_headers[MAXBUF] = {0};

    while ((n = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        // End of headers
        if (!strcmp(buf, "\r\n") || !strcmp(buf, "\n")) {
            break;
        }

        if (!strncasecmp(buf, "Host:", 5)) {
            has_own_host_header = true;
            strncpy(header_host, buf, sizeof(header_host) - 1);
        }
        // ignore client's own request header of User-Agent, Connection,
        // Proxy-Connection
        else if (!strncasecmp(buf, "User-Agent:", 11)) {
            continue;
        } else if (!strncasecmp(buf, "Connection:", 11)) {
            continue;
        } else if (!strncasecmp(buf, "Proxy-Connection:", 17)) {
            continue;
        }
        // Forward all remaining headers
        else {
            size_t current_len = strlen(remaining_headers);
            if (current_len + strlen(buf) < sizeof(remaining_headers)) {
                memcpy(remaining_headers + current_len, buf, strlen(buf) + 1);
            }
        }
    }

    if (!has_own_host_header) {
        if (strcmp(port, "80") != 0) {
            snprintf(header_host, sizeof(header_host), "Host: %s:%s\r\n", host,
                     port);
        } else {
            snprintf(header_host, sizeof(header_host), "Host: %s\r\n", host);
        }
    }

    // check if the request is cached befroe calling server
    const char *key = uri;
    cache_obj_t *obj = search_cache_obj(key);
    // hit
    if (obj) {
        size_t written_size = 0;
        while (written_size < obj->size) {
            size_t chunck_size = obj->size - written_size;
            if (chunck_size > MAXLINE) {
                chunck_size = MAXLINE;
            }

            if (rio_writen(connfd, obj->web_obj + written_size, chunck_size) <
                0) {
                break;
            }

            written_size += chunck_size;
        }
        free_cache_obj(obj);
        parser_free(parser);
        return;
    }

    /* 4. create request sent to end server*/
    //    combine client's headers and proxy's headers
    char request_line[MAXLINE];
    char whole_request[MAXBUF];

    snprintf(request_line, sizeof(request_line), "GET %s HTTP/1.0\r\n", path);

    snprintf(whole_request, sizeof(whole_request),
             "%s"
             "%s"
             "%s"
             "%s"
             "%s"
             "%s"
             "\r\n",
             request_line, header_host, header_user_agent, header_connection,
             header_proxy_connection, remaining_headers);

    /* 5. Act as a client, and send request to end server*/
    // viii. Open connection to the requested server and initialize a rio buffer
    // for it ix.   Write the http header into the server buffer x.    Read
    // responses off the server buffer and write them to the client buffer xi.
    // Free parser and close file descriptors
    int clientfd;
    rio_t client_rio;

    clientfd = open_clientfd(host, port);
    rio_readinitb(&client_rio, clientfd);
    // forward request to server
    if (rio_writen(clientfd, whole_request, strlen(whole_request)) < 0) {
        close(clientfd);
        parser_free(parser);
        return;
    }

    // forward response to client and save to buffer
    char web_obj_buffer[MAX_OBJECT_SIZE];
    size_t obj_size = 0; // offset
    bool cachable = true;

    while ((n = rio_readnb(&client_rio, buf, sizeof(buf))) > 0) {
        if (rio_writen(connfd, buf, (size_t)n) < 0) {
            cachable = false;
            break;
        }

        if (obj_size + (size_t)n <= MAX_OBJECT_SIZE) {
            memcpy(web_obj_buffer + obj_size, buf, (size_t)n);
            obj_size += (size_t)n;
        } else {
            cachable = false;
        }
    }
    if (cachable && obj_size > 0) {
        insert_cache_obj_to_cache(key, obj_size, web_obj_buffer);
    }

    close(clientfd);
    parser_free(parser);
}

/*
 * Thread rountine
 * modify from slides p.50
 */
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    // one request each time
    serve(connfd); // echo
    close(connfd);
    return NULL;
}

int main(int argc, char **argv) {
    // printf("%s", header_user_agent);

    // 1. Check arguments (argc/argv).
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // create cache
    init_cache();

    // 2. Set up listening socket with open_listenfd
    Signal(SIGPIPE, SIG_IGN);

    // 3. Run main server loop
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; /* Enough space for any addr */
    pthread_t tid;

    // 4. Within a thread:
    listenfd = open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (connfd < 0) {
            continue;
        }

        // to prevent race condition of local variable, connfd
        // save it to heap
        int *connfd_pt = malloc(sizeof(int));
        *connfd_pt = connfd;
        pthread_create(&tid, NULL, thread, connfd_pt);
    }

    return 0;
}
