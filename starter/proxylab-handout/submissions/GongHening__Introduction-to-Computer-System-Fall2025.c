/*
 * name: <龚鹤宁>
 * student-id: <2400012436>
 */
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Global cache */
static cache_t cache;

/* Function prototypes */
void *thread(void *vargp);
void doit(int connfd);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void build_http_request(char *http_request, char *hostname, char *path, 
                        char *port, char *client_host, const char *other_headers);
void clienterror(int fd, char *cause, char *errnum, 
                 char *shortmsg, char *longmsg);

/* Signal handler for SIGPIPE */
void sigpipe_handler(int sig) {
    return;  // Ignore SIGPIPE
}

int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* Ignore SIGPIPE */
    Signal(SIGPIPE, sigpipe_handler);

    /* Initialize cache */
    cache_init(&cache);

    /* Open listening socket */
    listenfd = Open_listenfd(argv[1]);
    
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

/* Thread routine */
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/* Handle one HTTP request/response transaction */
void doit(int connfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char http_request[MAXLINE * 2];
    char cache_key[MAXLINE];
    rio_t rio, server_rio;
    int serverfd;
    ssize_t n;
    char response_buf[MAX_OBJECT_SIZE];
    size_t response_size = 0;

    /* Read request line */
    Rio_readinitb(&rio, connfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        return;
    }
    
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    /* Only handle GET requests */
    if (strcasecmp(method, "GET")) {
        clienterror(connfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    /* Build cache key from URI */
    strcpy(cache_key, uri);

    /* Check cache first */
    cache_node_t *cached = cache_find(&cache, cache_key);
    if (cached) {
        /* Send cached content to client */
        Rio_writen(connfd, cached->content, cached->content_size);
        return;
    }

    /* Parse URI */
    if (parse_uri(uri, hostname, path, port) < 0) {
        clienterror(connfd, uri, "400", "Bad Request",
                    "Proxy could not parse the request");
        return;
    }

    /* Read request headers from client.
     * Keep Host (for forwarding) and forward all other headers except
     * User-Agent/Connection/Proxy-Connection, which we override per spec.
     */
    char client_host[MAXLINE] = "";
    char other_headers[MAXLINE * 8];
    other_headers[0] = '\0';
    char header_buf[MAXLINE];
    while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
        /* Host header */
        if (strncasecmp(buf, "Host:", 5) == 0) {
            strncpy(header_buf, buf, MAXLINE - 1);
            header_buf[MAXLINE - 1] = '\0';
            char *host_value = header_buf + 5;
            /* Skip whitespace */
            while (*host_value == ' ' || *host_value == '\t') {
                host_value++;
            }
            /* Remove trailing \r\n */
            char *end = strchr(host_value, '\r');
            if (end) {
                *end = '\0';
            }
            strncpy(client_host, host_value, MAXLINE - 1);
            client_host[MAXLINE - 1] = '\0';
            continue;
        }
        /* Skip headers that we will override */
        if (strncasecmp(buf, "User-Agent:", 11) == 0 ||
            strncasecmp(buf, "Connection:", 11) == 0 ||
            strncasecmp(buf, "Proxy-Connection:", 17) == 0) {
            continue;
        }
        /* Forward everything else as-is */
        if (strlen(other_headers) + strlen(buf) < sizeof(other_headers) - 1) {
            strcat(other_headers, buf);
        }
    }

    /* Build HTTP request to send to server */
    build_http_request(http_request, hostname, path, port, client_host, other_headers);

    /* Connect to server */
    if (strlen(port) == 0) {
        strcpy(port, "80");
    }
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(connfd, hostname, "502", "Bad Gateway",
                    "Proxy could not connect to the server");
        return;
    }

    /* Send request to server */
    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, http_request, strlen(http_request));

    /* Read response from server and forward to client */
    response_size = 0;
    int response_too_large = 0;
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        /* Forward to client */
        Rio_writen(connfd, buf, n);
        
        /* Cache the response if it fits */
        if (!response_too_large) {
            if (response_size + n <= MAX_OBJECT_SIZE) {
                memcpy(response_buf + response_size, buf, n);
                response_size += n;
            } else {
                /* Response is too large, don't cache */
                response_too_large = 1;
                response_size = 0;
            }
        }
    }

    /* Cache the full response if it fits */
    if (!response_too_large && response_size > 0 && response_size <= MAX_OBJECT_SIZE) {
        cache_insert(&cache, cache_key, response_buf, response_size);
    }

    Close(serverfd);
}

/* Parse URI to extract hostname, path, and port */
int parse_uri(char *uri, char *hostname, char *path, char *port) {
    char *hostname_ptr, *path_ptr, *port_ptr;
    char uri_copy[MAXLINE];
    
    strcpy(uri_copy, uri);
    
    /* Check if URI starts with http:// */
    if (strncasecmp(uri_copy, "http://", 7) != 0) {
        return -1;
    }

    hostname_ptr = uri_copy + 7;  // Skip "http://"
    
    /* Find path separator first */
    path_ptr = strchr(hostname_ptr, '/');
    
    /* Find port separator (before path if path exists) */
    port_ptr = strchr(hostname_ptr, ':');
    if (port_ptr && (!path_ptr || port_ptr < path_ptr)) {
        /* Port exists and is before path */
        *port_ptr = '\0';
        port_ptr++;
        /* Extract port number (until / or end) */
        if (path_ptr) {
            *path_ptr = '\0';
            strcpy(port, port_ptr);
            strcpy(path, path_ptr + 1);
        } else {
            /* No path, port goes to end */
            strcpy(port, port_ptr);
            strcpy(path, "");
        }
    } else {
        /* No port, or port is after path (shouldn't happen but handle it) */
        if (path_ptr) {
            *path_ptr = '\0';
            strcpy(path, path_ptr + 1);
        } else {
            strcpy(path, "");
        }
        strcpy(port, "");
    }

    strcpy(hostname, hostname_ptr);
    
    return 0;
}

/* Build HTTP request to send to server */
void build_http_request(char *http_request, char *hostname, char *path, 
                        char *port, char *client_host, const char *other_headers) {
    strcpy(http_request, "GET /");
    if (strlen(path) > 0) {
        strcat(http_request, path);
    }
    strcat(http_request, " HTTP/1.0\r\n");
    
    /* Host header - use client's Host header if provided, otherwise use parsed hostname */
    strcat(http_request, "Host: ");
    if (strlen(client_host) > 0) {
        strcat(http_request, client_host);
    } else {
        strcat(http_request, hostname);
        if (strlen(port) > 0 && strcmp(port, "80") != 0) {
            strcat(http_request, ":");
            strcat(http_request, port);
        }
    }
    strcat(http_request, "\r\n");
    
    /* User-Agent header */
    strcat(http_request, user_agent_hdr);
    
    /* Forward other client headers */
    if (other_headers && other_headers[0] != '\0') {
        strcat(http_request, other_headers);
    }

    /* Connection: close */
    strcat(http_request, "Connection: close\r\n");
    strcat(http_request, "Proxy-Connection: close\r\n");
    
    /* End of headers */
    strcat(http_request, "\r\n");
}

/* Send error response to client */
void clienterror(int fd, char *cause, char *errnum, 
                 char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    strcpy(body, "<html><title>Proxy Error</title>");
    strcat(body, "<body bgcolor=\"ffffff\">\r\n");
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    strcat(body, buf);
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    strcat(body, buf);
    strcat(body, "<hr><em>The Proxy Web server</em>\r\n");

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
