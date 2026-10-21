#include <stdio.h>
#include <stdlib.h>

#include "cache.h"
#include "csapp.h"

#define MAX_BUFFER 4096 /* Max temporary buffer size */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_LINE 10

typedef struct {
    char hostname[MAXLINE];
    char port[MAXLINE];
    char pathname[MAXLINE];
    char content[MAX_OBJECT_SIZE];
    size_t size;
    unsigned int lru;
} Cache;

/* Global variables */
pthread_rwlock_t rwlock;
Cache cache[MAX_CACHE_LINE];
unsigned int lrucnt;
static const char* user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* Function prototypes */
void cache_init(void);
int match(const char* hostname, const char* port, const char* pathname,
          Cache* cache);
static int parse_url(char* url, char* hostname, char* pathname, char* port);
void forward(int connfd);
void* thread(void* vargp);
void clienterror(int fd, char* cause, char* errnum, char* shortmsg,
                 char* longmsg);

void cache_init(void) {
    pthread_rwlock_init(&rwlock, NULL);
    lrucnt = 0;
    for (int i = 0; i < MAX_CACHE_LINE; i++) {
        cache[i].size = 0;
        cache[i].lru = 0;
    }
}

/* Check if hostname, port, pathname match */
int match(const char* hostname, const char* port, const char* pathname,
          Cache* cache) {
    return !strcasecmp(hostname, cache->hostname) &&
           !strcasecmp(port, cache->port) &&
           !strcasecmp(pathname, cache->pathname);
}

/* Parse URL into hostname, pathname, and port
 * Supported format:
 * http://www.cmu.edu/hub/index.html
 * http://www.cmu.edu:8080/hub/index.html
 * www.cmu.edu/hub/index.html
 * www.cmu.edu:8080/hub/index.html
 */
static int parse_url(char* url, char* hostname, char* pathname, char* port) {
    char* ptr = url;

    /* Check protocol */
    if (!strncasecmp(ptr, "http://", 7))
        ptr += 7;
    else if (!strncasecmp(ptr, "https://", 8))
        ptr += 8;

    /* Check hostname */
    while (*ptr != '/' && *ptr != ':' && *ptr != '\0') {
        *hostname++ = *ptr++;
    }
    *hostname = '\0';

    /* Check port */
    if (*ptr == ':') {
        ptr++;
        while (*ptr != '/' && *ptr != '\0') {
            *port++ = *ptr++;
        }
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Check pathname */
    if (*ptr == '/') {
        while (*ptr != '\0') {
            *pathname++ = *ptr++;
        }
    }
    *pathname = '\0';

    return 0;
}

/* Read and parse the request sent from clientfd, and forward it to the server
 * specified in the request.
 */
void forward(int connfd) {
    rio_t rio_client;
    Rio_readinitb(&rio_client, connfd);

    char reqline[MAXLINE];
    char method[MAX_BUFFER], url[MAX_BUFFER], version[MAX_BUFFER];

    /* Read request line */
    Rio_readlineb(&rio_client, reqline, MAXLINE);
    sscanf(reqline, "%s %s %s", method, url, version);

    /* Check method */
    if (strcasecmp(method, "GET")) {
        clienterror(connfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    /* Check version */
    if (strcasecmp(version, "HTTP/1.0") && strcasecmp(version, "HTTP/1.1")) {
        clienterror(connfd, version, "400", "Bad Request",
                    "Proxy could not understand this version");
        return;
    }

    /* Parse URL */
    char hostname[MAX_BUFFER], pathname[MAX_BUFFER], port[MAX_BUFFER];
    if (parse_url(url, hostname, pathname, port) < 0) {
        clienterror(connfd, url, "400", "Bad Request",
                    "Proxy could not parse this URL");
        return;
    }

    /* Read request headers */
    char reqhdr[MAXLINE];
    char fwd_reqhdr[MAXLINE];

    for (Rio_readlineb(&rio_client, reqhdr, MAXLINE); strcmp(reqhdr, "\r\n");
         Rio_readlineb(&rio_client, reqhdr, MAXLINE)) {
        if (!strncasecmp(reqhdr, "Host", 4)) {
            strcpy(hostname, reqhdr + 6);
            hostname[strlen(hostname) - 2] = '\0';
            char* ptr = strchr(hostname, ':');
            if (ptr) {
                *ptr = '\0';
                strcpy(port, ptr + 1);
            }
        } else if (!strncasecmp(reqhdr, "User-Agent", 10) ||
                   !strncasecmp(reqhdr, "Connection", 10) ||
                   !strncasecmp(reqhdr, "Proxy-Connection", 16))
            continue;
        else
            strcat(fwd_reqhdr, reqhdr);
    }

    /* Construct request */
    char request[MAXLINE];
    snprintf(request, sizeof request, "GET %s HTTP/1.0\r\n", pathname);
    snprintf(request + strlen(request), sizeof request, "Host: %s\r\n",
             hostname);
    snprintf(request + strlen(request), sizeof request, "%s", user_agent_hdr);
    snprintf(request + strlen(request), sizeof request,
             "Connection: close\r\n");
    snprintf(request + strlen(request), sizeof request,
             "Proxy-Connection: close\r\n");
    strcat(request, fwd_reqhdr);
    strcat(request, "\r\n");

    /* Retrieve from cache */
    for (int i = 0; i < MAX_CACHE_LINE; i++) {
        if (match(hostname, port, pathname, &cache[i])) {
            /* Cache hit */
            pthread_rwlock_rdlock(&rwlock); /* Read lock */
            Rio_writen(connfd, cache[i].content, cache[i].size);
            pthread_rwlock_unlock(&rwlock); /* Read unlock */
            return;
        }
    }

    /* Cache miss */

    /* Send request to server */
    rio_t rio_server;
    int clientfd = Open_clientfd(hostname, port);
    Rio_readinitb(&rio_server, clientfd);
    Rio_writen(clientfd, request, strlen(request));

    /* Forward response to client */
    char response[MAX_OBJECT_SIZE];
    char buf[MAXLINE];
    int n;
    size_t size = 0;
    while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) != 0) {
        Rio_writen(connfd, buf, n);
        if (size + n < MAX_OBJECT_SIZE) memcpy(response + size, buf, n);
        size += n;
    }
    Close(clientfd);

    /* Cache response */
    int min = 0;
    for (int i = 0; i < MAX_CACHE_LINE; i++) {
        if (cache[i].size == 0) {
            min = i;
            break;
        }
        if (cache[i].lru < cache[min].lru) min = i;
    }
    if (size < MAX_OBJECT_SIZE) {
        pthread_rwlock_wrlock(&rwlock); /* Write lock */
        strcpy(cache[min].hostname, hostname);
        strcpy(cache[min].port, port);
        strcpy(cache[min].pathname, pathname);
        memcpy(cache[min].content, response, size);
        cache[min].size = size;
        cache[min].lru = ++lrucnt;
        pthread_rwlock_unlock(&rwlock); /* Write unlock */
    }
    pthread_rwlock_destroy(&rwlock);
}

void clienterror(int fd, char* cause, char* errnum, char* shortmsg,
                 char* longmsg) {
    char buf[MAX_BUFFER] = "";
    char body[MAXBUF] = "";

    /* Build the HTTP response body */
    snprintf(body + strlen(body), sizeof body,
             "<html><title>Tiny Error</title>");
    snprintf(body + strlen(body), sizeof body, "<body bgcolor=ffffff>\r\n");
    snprintf(body + strlen(body), sizeof body, "%s: %s\r\n", errnum,
             shortmsg);
    snprintf(body + strlen(body), sizeof body, "<p>%s: %s\r\n", longmsg,
             cause);
    snprintf(body + strlen(body), sizeof body,
             "<hr><em>The Tiny Web server</em>\r\n");

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

void* thread(void* vargp) {
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    forward(connfd);
    Close(connfd);
    return NULL;
}

/* Listen for incoming connections on a specified port */
int main(int argc, char** argv) {
    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    char* port = argv[1];
    int listenfd = Open_listenfd(port);

    /* Ignore SIGPIPE */
    Signal(SIGPIPE, SIG_IGN);

    /* Initialization*/
    cache_init();

    while (1) {
        struct sockaddr_storage clientaddr;
        socklen_t clientlen = sizeof clientaddr;
        int* connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*)&clientaddr, &clientlen);

        pthread_t tid;
        Pthread_create(&tid, NULL, (void*)&thread, connfdp);
    }
    Close(listenfd);
}
