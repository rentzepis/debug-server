#include <stdio.h>
#include <unistd.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAX_THREAD 256

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void parseUri(char* uri, char* host, char* port, char* location);
void doit(int fd);

void* thread_routine(void* arg);

Cache cache;
int timestamp;

int main(int argc, char* argv[])
{
    int connfd, listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t thread_id[MAX_THREAD];

    cache_init();

    sbuf_t* sbuf = (sbuf_t*)Malloc(sizeof(sbuf_t));
    sbuf_init(sbuf, MAX_THREAD);

    for (int i = 0; i < MAX_THREAD; i++) {
        Pthread_create(&thread_id[i], NULL, thread_routine, (void *)sbuf);
    }

    Signal(SIGINT, SIG_IGN);
    Signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(sbuf, connfd);
    }

    sbuf_free(sbuf);
    Free(sbuf);
    return 0;
}

void doit(int fd) {
    char buf[MAXLINE], method[20], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], location[MAXLINE], port[MAXLINE];
    char msg[MAXLINE];
    char received_msg[MAXLINE];
    rio_t rio, client_rio;
    int clientfd;
    struct addrinfo hints;
    struct addrinfo *res, *p;
    ssize_t len = 0;
    // size_t content_length;
    char response[MAX_OBJECT_SIZE];
    int able_to_save = 1;

    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) 
        return;
    sscanf(buf, "%s %s %s", method, uri, version);
    memset(&host, 0, sizeof(host));
    memset(&port, 0, sizeof(port));
    memset(&location, 0, sizeof(location));
    parseUri(uri, host, port, location);

    int suc = cache_read(uri, fd);
    if (suc) return;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    Getaddrinfo(host, port, &hints, &res);
    for (p = res; p != NULL; p = p->ai_next) {
        clientfd = Socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        Close(clientfd);
        clientfd = -1;
    }
    
    Freeaddrinfo(res);

    if (clientfd == -1) {
        fprintf(stderr, "502 Bad Gateway for server: %s:%s\n", host, port);
        return;
    }

    Rio_readinitb(&client_rio, clientfd);

    sprintf(msg, "%s ", method);
    Rio_writen(clientfd, msg, strlen(msg));
    sprintf(msg, "%s", location);
    Rio_writen(clientfd, msg, strlen(msg));
    sprintf(msg, " HTTP/1.0\r\n");
    Rio_writen(clientfd, msg, strlen(msg));
    sprintf(msg, "Host: ");
    Rio_writen(clientfd, msg, strlen(msg));
    sprintf(msg, "%s", host);
    Rio_writen(clientfd, msg, strlen(msg));
    sprintf(msg, "\r\n%s", user_agent_hdr);
    Rio_writen(clientfd, msg, strlen(msg));
    sprintf(msg, "Connection: Close\r\nProxy-Connection: Close\r\n\r\n");
    Rio_writen(clientfd, msg, strlen(msg));

    ssize_t n;
    while ((n = Rio_readnb(&client_rio, received_msg, MAXLINE)) > 0) {
        len += n;
        if (len > MAX_OBJECT_SIZE) able_to_save = 0;
        Rio_writen(fd, received_msg, n);
    }

    if (able_to_save) {
        cache_write(received_msg, uri, len);
    }

    // while((len = Rio_readlineb(&client_rio, received_msg, MAXLINE)) > 0) {
    //     Rio_writen(fd, received_msg, strlen(received_msg));
    //     if (received_msg[0] == '\r') break;
    //     char* ptr = strstr(received_msg, "Content-length");
    //     if (ptr == NULL) continue;
    //     content_length = atoi(received_msg + 16);
    // }

    // for (len = 0; len < content_length / MAXLINE; len++) {
    //     Rio_readnb(&client_rio, received_msg, MAXLINE);
    //     Rio_writen(fd, received_msg, MAXLINE);
    // }
    // if (len * MAXLINE < content_length) {
    //     ssize_t remaining_size = content_length - len * MAXLINE;
    //     Rio_readnb(&client_rio, received_msg, remaining_size);
    //     Rio_writen(fd, received_msg, remaining_size);
    // }

    Close(clientfd);
}

void parseUri(char* uri, char* host, char* port, char* location) {
    char* ptr_host = strstr(uri, "://");
    strncpy(host, ptr_host + 3, strlen(ptr_host + 3));
    char* ptr_location = strchr(host, '/');
    if (ptr_location == NULL) {
        location = "/";
    } else {
        strncpy(location, ptr_location, strlen(ptr_location));
        *ptr_location = '\0';
    }
    char* ptr = strchr(host, ':');
    if (ptr) {
        strncpy(port, ptr+1, strlen(ptr));
        *ptr = '\0';
    }
    return;
}

void* thread_routine(void* arg) {
    Pthread_detach(Pthread_self());
    sbuf_t* sbuf = (sbuf_t*) arg;
    while (1) {
        int connfd = sbuf_remove(sbuf);
        doit(connfd);
        close(connfd);
    }
}
