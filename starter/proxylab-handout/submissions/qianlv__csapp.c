#include "csapp.h"

#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void parse_request(rio_t* rp, char* url) {
    char buf[MAXLINE];
    int n;
    while ((n = Rio_readlineb(rp, buf, MAXLINE)) > 0) {
        /* printf("%s", buf); */
        if (strncmp(buf, "\r\n", 2) == 0) {
            break;
        }
        if (strncmp(buf, "GET", 3) == 0) {
            char *ptr = strchr(buf + 4, ' ');
            *ptr = '\0';
            strcpy(url, buf + 4);
        }
    }
}

void parse_url(char* host, char* port, char* filename, const char* url) {
    // skip "http://"
    char* ptr = strchr(url, ':');
    ptr += 3;
    char* next = strchr(ptr, ':');
    if (next != NULL) {
        strncpy(host, ptr, next - ptr);
        ptr = next + 1;

        next = strchr(ptr, '/');
        if (next == NULL) {
            strcpy(port, ptr);
        } else {
            strncpy(port, ptr, next - ptr);
        }

        ptr = next;
    } else {
        next = strchr(ptr, '/');
        if (next == NULL) {
            strcpy(host, ptr);
        } else {
            strncpy(host, ptr, next - ptr);
        }
        ptr = next;
    }

    if (ptr == NULL) {
        strcpy(filename, "/");
    } else {
        strcpy(filename, ptr);
    }
}

void* serve(void* arg) {
    Pthread_detach(Pthread_self());
    int fd = *(int*)arg;
    int n;
    rio_t rp;
    rio_readinitb(&rp, fd);
    char url[MAXLINE] = {0};
    parse_request(&rp, url);
    char host[MAXLINE], port[MAXLINE], filename[MAXLINE];
    memset(host, 0, MAXLINE * sizeof(char));
    memset(port, 0, MAXLINE * sizeof(char));
    memset(filename, 0, MAXLINE * sizeof(char));
    strncpy(port, "80", 3);
    parse_url(host, port, filename, url);
    /* printf("%s %s %s\n", host, port, filename); */

    int ser_fd = Open_clientfd(host, port);
    char buf[MAXLINE];
    sprintf(buf, "GET %s HTTP/1.0\r\n", filename);
    sprintf(buf, "%sHost: %s:%s\r\n", buf, host, port);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\nProxy-Connection: close\r\n\r\n", buf);
    /* printf("%s", buf); */
    Rio_writen(ser_fd, buf, strlen(buf));
    while ((n = Rio_readn(ser_fd, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
    }
    Close(fd);
    Close(ser_fd);
    free(arg);
    return NULL;
}

int main(int argc, char *argv[])
{
    pthread_t pid;
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; /* Enough room for any addr */
    char client_hostname[MAXLINE], client_port[MAXLINE];
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage); /* Important! */
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen,
                client_hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        int *fd = Malloc(sizeof(int));
        *fd = connfd;
        Pthread_create(&pid, NULL, serve, fd);
        printf("--- %ld\n", pid);
    }
    return 0;
}
