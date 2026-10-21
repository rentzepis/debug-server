#include <signal.h>
#include <stdio.h>

#include "cache.h"
#include "csapp.h"

void *thread(void *vargp);
void doit(int clientfd);
void read_requesthdrs(rio_t *rp, void *buf, int serverfd, char *hostname, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *hostname, char *port, char *path);

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
    int listenfd, *clientfd;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    signal(SIGPIPE, SIG_IGN);

    rootp = (web_object_t *)calloc(1, sizeof(web_object_t));
    lastp = (web_object_t *)calloc(1, sizeof(web_object_t));

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        clientfd = Malloc(sizeof(int));
        *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        // if connection is accepted, create thread doit();
        // main thread is ready for another request.
        Pthread_create(&tid, NULL, thread, clientfd);
    }
}

void *thread(void *vargp) {
    int clientfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(clientfd);
    Close(clientfd);
    return NULL;
}

void doit(int clientfd) {
    int serverfd, content_length;
    char request_buf[MAXLINE], response_buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE], port[MAXLINE];
    char *response_ptr, filename[MAXLINE], cgiargs[MAXLINE];
    rio_t request_rio, response_rio;

    // client to proxy
    Rio_readinitb(&request_rio, clientfd);
    Rio_readlineb(&request_rio, request_buf, MAXLINE);
    printf("Request headers:\n %s\n", request_buf);

    sscanf(request_buf, "%s %s", method, uri);
    parse_uri(uri, hostname, port, path);

    sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");

    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        clienterror(clientfd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }

    // find cache
    web_object_t *cached_object = find_cache(path);
    if (cached_object) {
        send_cache(cached_object, clientfd);
        read_cache(cached_object);
        return;
    }

    // proxy to server
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(serverfd, method, "502", "Bad Gateway", "📍 Failed to establish connection with the end server");
        return;
    }
    Rio_writen(serverfd, request_buf, strlen(request_buf));

    // server to proxy
    read_requesthdrs(&request_rio, request_buf, serverfd, hostname, port);
    Rio_readinitb(&response_rio, serverfd);
    while (strcmp(response_buf, "\r\n")) {
        Rio_readlineb(&response_rio, response_buf, MAXLINE);
        if (strstr(response_buf, "Content-length"))
            content_length = atoi(strchr(response_buf, ':') + 1);
        Rio_writen(clientfd, response_buf, strlen(response_buf));
    }
    response_ptr = malloc(content_length);
    Rio_readnb(&response_rio, response_ptr, content_length);

    // proxy to client
    Rio_writen(clientfd, response_ptr, content_length);

    // caching
    if (content_length <= MAX_OBJECT_SIZE) {
        web_object_t *web_object = (web_object_t *)calloc(1, sizeof(web_object_t));
        web_object->response_ptr = response_ptr;
        web_object->content_length = content_length;
        strcpy(web_object->path, path);
        write_cache(web_object);
    } else
        free(response_ptr);

    Close(serverfd);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    Rio_writen(fd, body, strlen(body));
}

void parse_uri(char *uri, char *hostname, char *port, char *path) {
    char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
    char *port_ptr = strchr(hostname_ptr, ':');
    char *path_ptr = strchr(hostname_ptr, '/');
    strcpy(path, path_ptr);

    if (port_ptr) {
        strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
        strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
    } else {
        strcpy(port, "8000");
        strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
    }
}

void read_requesthdrs(rio_t *request_rio, void *request_buf, int serverfd, char *hostname, char *port) {
    int is_host_exist;
    int is_connection_exist;
    int is_proxy_connection_exist;
    int is_user_agent_exist;

    Rio_readlineb(request_rio, request_buf, MAXLINE);
    while (strcmp(request_buf, "\r\n")) {
        if (strstr(request_buf, "Proxy-Connection") != NULL) {
            sprintf(request_buf, "Proxy-Connection: close\r\n");
            is_proxy_connection_exist = 1;
        } else if (strstr(request_buf, "Connection") != NULL) {
            sprintf(request_buf, "Connection: close\r\n");
            is_connection_exist = 1;
        } else if (strstr(request_buf, "User-Agent") != NULL) {
            sprintf(request_buf, user_agent_hdr);
            is_user_agent_exist = 1;
        } else if (strstr(request_buf, "Host") != NULL) {
            is_host_exist = 1;
        }

        Rio_writen(serverfd, request_buf, strlen(request_buf));
        Rio_readlineb(request_rio, request_buf, MAXLINE);
    }

    if (!is_proxy_connection_exist) {
        sprintf(request_buf, "Proxy-Connection: close\r\n");
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }
    if (!is_connection_exist) {
        sprintf(request_buf, "Connection: close\r\n");
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }
    if (!is_host_exist) {
        sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }
    if (!is_user_agent_exist) {
        sprintf(request_buf, user_agent_hdr);
        Rio_writen(serverfd, request_buf, strlen(request_buf));
    }

    sprintf(request_buf, "\r\n");
    Rio_writen(serverfd, request_buf, strlen(request_buf));
    return;
}