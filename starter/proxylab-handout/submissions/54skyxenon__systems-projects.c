#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended object size */
#define MAX_OBJECT_SIZE 102400

/* Other constants */
#define MAX_HTTP_VERB_LENGTH 8

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/*
 * Duplicates a stack-allocated string to dynamic memory.
 */
static char* stringcpy(const char* original) {
    char *copy = Malloc(strlen(original) + 1);
    strcpy(copy, original);
    copy[strlen(original)] = '\0';
    return copy;
}

/*
 * Helper for sending response for what went wrong.
 */
static void clienterror(int connfd, char* errnum, char* shortmsg, char* longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];

    // send the HTTP response headers
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(connfd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(connfd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(connfd, buf, strlen(buf));

    // send the HTTP response body
    sprintf(body, "<html><title>Proxy Error</title><body>%s</body></html>", longmsg);
    Rio_writen(connfd, body, strlen(body));
}

/*
 * The important request headers for this lab are:
 * - Host
 * - User-Agent
 * - Connection
 * - Proxy-Connection
 *
 * Include them in a request to the real server, and write the real response back to the socket.
 */
static void request(int clientfd, int connfd, char* host_header_value, char* path, char* uri) {
    char buf[MAXLINE];
    char* object_buf = Malloc(MAX_OBJECT_SIZE); // for cache

    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    Rio_writen(clientfd, buf, strlen(buf));
    sprintf(buf, "Host: %s\r\n", host_header_value);
    Rio_writen(clientfd, buf, strlen(buf));
    sprintf(buf, "%s", user_agent_hdr);
    Rio_writen(clientfd, buf, strlen(buf));
    sprintf(buf, "Connection: close\r\n");
    Rio_writen(clientfd, buf, strlen(buf));
    sprintf(buf, "Proxy-Connection: close\r\n\r\n");
    Rio_writen(clientfd, buf, strlen(buf));

    rio_t rio;
    int n;
    int object_buf_index = 0;
    int can_cache = 1;

    Rio_readinitb(&rio, clientfd);
    while ((n = Rio_readnb(&rio, buf, MAXLINE)) != 0) {
        if (can_cache && (MAX_OBJECT_SIZE - object_buf_index >= n)) {
            memcpy(object_buf + object_buf_index, buf, n);
            object_buf_index += n;
        } else {
            can_cache = 0;
        }
        Rio_writen(connfd, buf, n);
    }

    if (can_cache) {
        CacheEntry* entry = (CacheEntry*) Malloc(sizeof(CacheEntry));
        entry->uri = stringcpy(uri);
        entry->size = object_buf_index;
        entry->data = Realloc(object_buf, object_buf_index);
        entry->timestamp = (int)time(NULL);
        add_cached_uri_entry(entry);
    } else {
        Free(object_buf);
    }
}

/*
 * Get the exact resource path after the hostname in the URI.
 */
static char* get_path(char* uri) {
    uri = strdup(uri);

    // sent by browser
    if (uri[0] == '/') {
        uri++;
    }

    // skip this portion
    if (strstr(uri, "http://") != NULL) {
        uri += 7;
    } else if (strstr(uri, "https://") != NULL) {
        uri += 8;
    }

    // and just get the start of the path
    char* path = strstr(uri, "/");
    if (path == NULL) {
        strcat(uri, "/");
        path = strstr(uri, "/");
    }
    return path;
}

/*
 * Reads the hostname and port from the uri string, by reference.
 */
static void parse_uri(char* uri, char* hostname, char* port) {
    // sent by browser
    if (uri[0] == '/') {
        uri++;
    }

    // skip this part
    if (strstr(uri, "http://") != NULL) {
        uri += 7;
    } else if (strstr(uri, "https://") != NULL) {
        uri += 8;
    }

    char* path_ptr = strstr(uri, "/");
    if (path_ptr == NULL) {
        strcat(uri, "/");
        path_ptr = strstr(uri, "/");
    }
    int path_index = path_ptr - uri;

    char* port_ptr = strstr(uri, ":");

    if (port_ptr == NULL) {
        strncpy(hostname, uri, path_index);
        hostname[path_index] = '\0';
        strcpy(port, "80");
    } else {
        int port_index = port_ptr - uri;
        strncpy(hostname, uri, port_index);
        hostname[port_index] = '\0';

        int port_chars_to_copy = path_index - (port_index + 1);
        strncpy(port, port_ptr + 1, port_chars_to_copy);
        port[port_chars_to_copy] = '\0';
    }
}

/*
 * Do all the work for part I and III.
 *
 * ! KISS for this small lab:
 * - Forget about time complexity. I won't code up a complex data structure and opt to just linear search for the oldest entry to delete.
 * - No TTL on cache entries, it's just always there.
 */
static void fetch_and_respond(int connfd) {
    char buf[MAXLINE];
    rio_t rio;

    // read request line
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);

    char method[MAX_HTTP_VERB_LENGTH], uri[MAXLINE], version[MAXLINE];
    sscanf(buf, "%s %s %s", method, uri, version);

    // check cache here
    CacheEntry* entry = get_cached_uri_entry(uri);
    if (entry != NULL) {
        printf("Cache hit for URI %s!\n", uri);
        bump_timestamp_for_cached_uri(uri);
        Rio_writen(connfd, entry->data, entry->size);
        return;
    }

    // check for host header
    char header[MAXLINE], host_header_value[MAXLINE];
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(&rio, buf, MAXLINE);
        if (strstr(buf, "Host: ") != NULL) {
            sscanf(buf, "%s %s", header, host_header_value);
            break;
        }
    }

    // don't need to support POST
    if (strcasecmp(method, "GET")) {
        sprintf(buf, "Proxy does not implement method %s\n", method);
        clienterror(connfd, "501", "Not implemented", buf);
        return;
    }

    char hostname[MAXLINE], port[MAXLINE];
    parse_uri(uri, hostname, port);

    if (!strlen(hostname) || !strlen(port)) {
        sprintf(buf, "Empty hostname or port number, dropping request.");
        clienterror(connfd, "404", "Not found", buf);
        return;
    }

    // this happens with Firefox
    if (!strcmp(hostname, "favicon.ico")) {
        sprintf(buf, "Ignoring browser request for favicon.ico");
        clienterror(connfd, "501", "Not implemented", buf);
        return;
    }

    char* path = get_path(uri);
    printf("Calling open_clientfd(%s, %s) with path %s\n", hostname, port, path);
    int clientfd = Open_clientfd(hostname, port);
    request(clientfd, connfd, host_header_value, path, uri);
}

/*
 * Detaches itself to not block the main thread, then handles rest of HTTP transaction.
 */
static void* worker_thread(void* connfdp) {
    Pthread_detach(pthread_self());

    int connfd = *((int*) connfdp);
    Free(connfdp);
    fetch_and_respond(connfd);
    Close(connfd);

    return NULL;
}

/*
 * Example test commands:
 * 1. ./proxy 15213
 * 2. ./tiny/tiny 15214
 * 3. curl --proxy http://localhost:15213 http://localhost:15214/path/to/asset
 */
int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    init_cache();

    int listenfd = Open_listenfd(argv[1]);
    int* connfdp;
    pthread_t tid;

    while (1) {
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, NULL, NULL);
        Pthread_create(&tid, NULL, worker_thread, connfdp);
    }
}
