// filepath: /concurrent-web-proxy/proxy.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "csapp.h"

#define MAXLINE 8192
#define LISTENQ 1024

void *handle_client(void *arg);
void log_request(const char *client_ip, const char *url, size_t response_size);

int main(int argc, char **argv) {
    int listenfd, *connfd;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = malloc(sizeof(int));
        *connfd = Accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        pthread_create(&tid, NULL, handle_client, connfd);
    }

    return 0;
}

void *handle_client(void *arg) {
    int connfd = *(int *)arg;
    free(arg);
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int port;
    size_t response_size = 0;

    Rio_readlineb(rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "CONNECT") != 0) {
        Close(connfd);
        return NULL;
    }

    // Parse URI and handle request (omitted for brevity)
    // Assume we have a function to handle the request and get response_size

    // Log the request
    log_request(inet_ntoa(clientaddr.sin_addr), uri, response_size);

    Close(connfd);
    return NULL;
}

void log_request(const char *client_ip, const char *url, size_t response_size) {
    FILE *log_file = fopen("proxy.log", "a");
    if (log_file != NULL) {
        fprintf(log_file, "Client IP: %s, URL: %s, Response Size: %zu bytes\n", client_ip, url, response_size);
        fclose(log_file);
    }
}