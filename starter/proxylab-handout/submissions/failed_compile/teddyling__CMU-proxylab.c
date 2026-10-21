/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

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
#define HOSTLEN 256
#define PORTLEN 8
typedef struct sockaddr SA;

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
// static const char *header_user_agent = "Mozilla/5.0"
//" (X11; Linux x86_64; rv:3.10.0)"
//" Gecko/20221116 Firefox/63.0.1";
static const char *header_user_agent =
    "Mozilla/5.0 (X11; Linux x86_64; rv:3.10.0) Gecko/20221116 Firefox/63.0.1";

void parseInput(int);
int checkAndReadHeaders(int connfd, parser_t *parser, rio_t *rio,
                        char *userHostHeader, char *userOtherHeader);
void buildReqAndHeader(const char *method, const char *host, const char *path,
                       const char *port, char *userSentHost,
                       char *userSentOther, char *rebuilt);
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg);

void sigpipe_handler(int sig);
int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    int listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }
    Signal(SIGPIPE, sigpipe_handler);

    int connfd;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    char client_hostname[HOSTLEN];
    char client_port[PORTLEN];
    while (true) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (connfd < 0) {
            perror("accept");
            continue;
        }
        int gni = getnameinfo((SA *)&clientaddr, clientlen, client_hostname,
                              HOSTLEN, client_port, PORTLEN, 0);
        if (gni == 0) {
            printf("Accepted connection from: %s:%s\n", client_hostname,
                   client_port);
        } else {
            fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(gni));
        }
        parseInput(connfd);
        close(connfd);
    }

    return 0;
}
void parseInput(int connfd) {
    rio_t rio;
    rio_readinitb(&rio, connfd);
    char buffer[MAXLINE];
    int ReadLineRes = rio_readlineb(&rio, buffer, sizeof(buffer));
    if (ReadLineRes == 0) {
        printf("Empty Line\n");
        return;
    }
    if (ReadLineRes < 0) {
        printf("rio_readline error\n");
        return;
    }
    printf("%s", buffer);
    parser_t *parser = parser_new();
    parser_state state = parser_parse_line(parser, buffer);
    if (state != REQUEST) {
        parser_free(parser);
        clienterror(connfd, "400", "Bad Request",
                    "Proxy received a malformed request");
        return;
    }
    const char *method;
    if (parser_retrieve(parser, METHOD, &method) < 0) {
        parser_free(parser);
        printf("parser_retreieve error\n");
        return;
    }
    if (strcmp(method, "POST") == 0) {
        parser_free(parser);
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy does not implement POST method");
        return;
    }
    if (strcmp(method, "GET") != 0) {
        parser_free(parser);
        clienterror(connfd, "501", "Not Implemented",
                    "Proxy does not implement this method");
    }
    const char *host;
    const char *scheme;
    const char *uri;
    const char *port;
    const char *path;
    const char *http_version;
    parser_retrieve(parser, HOST, &host);
    parser_retrieve(parser, SCHEME, &scheme);
    parser_retrieve(parser, URI, &uri);
    parser_retrieve(parser, PORT, &port);
    parser_retrieve(parser, PATH, &path);
    parser_retrieve(parser, HTTP_VERSION, &http_version);
    char userSentHostHeader[MAXLINE];
    char userSentOtherHeader[MAXLINE];
    userSentHostHeader[0] = 0;
    userSentOtherHeader[0] = 0;
    if (checkAndReadHeaders(connfd, parser, &rio, userSentHostHeader,
                            userSentOtherHeader)) {
        parser_free(parser);
        return;
    }
    char reBuiltReqAndHead[MAXLINE];
    buildReqAndHeader(method, host, path, port, userSentHostHeader,
                      userSentOtherHeader, reBuiltReqAndHead);
    int clientfd = open_clientfd(host, port);
    if (clientfd < 0) {
        parser_free(parser);
        fprintf(stderr,
                "Failed to establish connection with server on host: %s, port: "
                "%s\n",
                host, port);
        return;
    }

    if (rio_writen(clientfd, reBuiltReqAndHead, sizeof(reBuiltReqAndHead)) <
        0) {
        parser_free(parser);
        fprintf(stderr, "Error writing to server\n");
        return;
    }
    printf("Things sent to server: \n");
    printf("%s", reBuiltReqAndHead);
    rio_t rio2;
    rio_readinitb(&rio2, clientfd);
    char response[MAXLINE];
    int num;
    while ((num = rio_readnb(&rio2, response, MAXLINE)) > 0) {
        printf("%s\n", response);
        rio_writen(connfd, response, num);
    }
    parser_free(parser);
    close(clientfd);
}
int checkAndReadHeaders(int connfd, parser_t *parser, rio_t *rio,
                        char *userHostHeader, char *userOtherHeader) {
    char buffer[MAXLINE];
    char helper[MAXLINE];
    while (1) {
        if (rio_readlineb(rio, buffer, sizeof(buffer)) <= 0) {
            return 1;
        }
        if (strcmp(buffer, "\r\n") == 0) {
            return 0;
        }
        parser_state state = parser_parse_line(parser, buffer);
        if (state != HEADER) {
            clienterror(connfd, "400", "Bad Request",
                        "Proxy received malformed request headers");
            return 0;
        }
        header_t *header = parser_retrieve_next_header(parser);
        if (strcmp(header->name, "Host") == 0) {
            sprintf(userHostHeader, "%s: %s\r\n", header->name, header->value);
        }
        if ((strcmp(header->name, "User-Agent") != 0) &&
            (strcmp(header->name, "Connection") != 0) &&
            (strcmp(header->name, "Proxy-Connection") != 0)) {
            if (userOtherHeader[0] == 0) {
                sprintf(userOtherHeader, "%s: %s\r\n", header->name,
                        header->value);
            } else {
                sprintf(helper, "%s: %s\r\n", header->name, header->value);
                strcat(userOtherHeader, helper);
                // sprintf(userOtherHeader, "%s%s: %s\r\n", userOtherHeader,
                // header->name, header->value);
            }
        }
        printf("%s: %s\n", header->name, header->value);
    }
}
void buildReqAndHeader(const char *method, const char *host, const char *path,
                       const char *port, char *userSentHost,
                       char *userSentOther, char *rebuilt) {
    char helper[MAXLINE];
    sprintf(rebuilt, "%s %s HTTP/1.0\r\n\r\n", method, path);
    if (userSentHost[0] != 0) {
        strcat(rebuilt, userSentHost);
    } else {
        sprintf(helper, "Host: %s:%s\r\n", host, port);
        strcat(rebuilt, helper);
    }
    sprintf(helper, "User-Agent: %s\r\n", header_user_agent);
    strcat(rebuilt, helper);
    strcat(rebuilt, "Connection: close\r\n");
    strcat(rebuilt, "Proxy-Connection: close\r\n");
    if (userSentOther[0] != 0) {
        strcat(rebuilt, userSentOther);
    }
    strcat(rebuilt, "\r\n");

    // printf(rebuilt, "%s %s", rebuilt, path);
    // sprintf(rebuilt, "%s HTTP/1.0\r\n", rebuilt);
    // sprintf(rebuilt, "%s\r\n", rebuilt);
    // if (userSentHost[0] != 0) {
    // sprintf(rebuilt, "%s%s", rebuilt, userSentHost);
    //} else {
    // sprintf(rebuilt, "%sHost: %s:%s\r\n", rebuilt, host, port);
    //}
    // sprintf(rebuilt, "%sUser-Agent: %s\r\n", rebuilt, header_user_agent);
    // sprintf(rebuilt, "%sConnection: close\r\n", rebuilt);
    // sprintf(rebuilt, "%sProxy-Connection: close\r\n", rebuilt);
    // if (userSentOther[0] != 0) {
    // sprintf(rebuilt, "%s%s", rebuilt, userSentOther);
    //}
    // sprintf(rebuilt, "%s\r\n", rebuilt);
}
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

void sigpipe_handler(int sig) {
    return;
}
