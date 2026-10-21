#include <stdio.h>
#include "csapp.h"
#include "tiny/csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static int cache_used = 0;
static sem_t mutex;

struct Cache{
    char url[MAXLINE];
    int history;
    char content[MAX_OBJECT_SIZE];
    int size;
} cache[8];

void *thread(void *vargp);

int cache_load(char url[MAXLINE]) {
    for (int i = 0; i < cache_used; i++) {
        if (strcmp(cache[i].url, url) == 0){
            return i;
        }
    }
    return -1;
}

void cache_save(char url[MAXLINE], int size, char content[MAX_OBJECT_SIZE]) {
    int max_history_index = 0;
    int max_history = 0;
    for (int i = 0; i < cache_used; i++){
        if (strcmp(cache[i].url, url) == 0){
            cache[i].history = 0;
            return;
        } else {
            cache[i].history ++;
        }
        if (cache[i].history > max_history){
            max_history_index = i;
            max_history = cache[i].history;
        }
    }
    if (cache_used < 8){
        memcpy(cache[cache_used].url, url, MAXLINE);
        memcpy(cache[cache_used].content, content, MAX_OBJECT_SIZE);
        cache[cache_used].history = 0;
        cache[cache_used].size = size;
        cache_used ++;
    } else {
        memcpy(cache[max_history_index].url, url, MAXLINE);
        memcpy(cache[max_history_index].content, content, MAX_OBJECT_SIZE);
        cache[max_history_index].history = 0;
        cache[max_history_index].size = size;
    }
}

void process(int connfd) {
    size_t n;
    char buf[MAXLINE];
    rio_t rio;
    int clientfd;
    int i;

    //processing "GET http://www.cmu.edu:8080/hub/index.html HTTP/1.1"
    char client_hostname[MAXLINE], client_port[MAXLINE], client_path[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    Rio_readinitb(&rio, connfd);
    if (Rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        return; 
    }
    sscanf(buf, "%s %s %s", method, uri, version);
    char uri_tmp[MAXLINE];
    strcpy(uri_tmp, uri);
    
    char *host_start = strstr(uri, "http://") ? uri + 7 : uri;
    char *port_start = strchr(host_start, ':');
    char *path_start = strchr(host_start, '/');

    if (path_start == NULL) {
        strcpy(client_path, "/"); 
    } else {
        strcpy(client_path, path_start);
        *path_start = '\0';
    }

    if (port_start == NULL) {
        strcpy(client_port, "80");
        strcpy(client_hostname, host_start);
    } else {
        strcpy(client_port, port_start + 1);
        *port_start = '\0';
        strcpy(client_hostname, host_start);
    }

    char tmp_buf[MAXLINE];
    Rio_readlineb(&rio, tmp_buf, MAXLINE);
    while (strcmp(tmp_buf, "\r\n") && strlen(tmp_buf) > 0) {
        Rio_readlineb(&rio, tmp_buf, MAXLINE);
    }

    P(&mutex);

    if ((i = cache_load(uri_tmp)) != -1){
        cache_save(uri_tmp, cache[i].size, cache[i].content);
        Rio_writen(connfd, cache[i].content, cache[i].size);
        V(&mutex);
        return;
    }

    V(&mutex);

    clientfd = Open_clientfd(client_hostname, client_port);
    if (clientfd < 0){
        return;
    }

    char request[MAX_OBJECT_SIZE];
    sprintf(request, "GET %s HTTP/1.0\r\n", client_path);
    sprintf(request + strlen(request), "Host: %s\r\n", client_hostname);
    sprintf(request + strlen(request), "%s", user_agent_hdr);
    sprintf(request + strlen(request), "Connection: close\r\n");
    sprintf(request + strlen(request), "Proxy-Connection: close\r\n");
    sprintf(request + strlen(request), "\r\n");
    n = strlen(request);
    Rio_writen(clientfd, request, n);

    rio_t rio_server;
    char server_buf[MAXLINE];
    ssize_t read_bytes;

    Rio_readinitb(&rio_server, clientfd);
    int size = 0;
    char payload[MAX_OBJECT_SIZE];
    while ((read_bytes = Rio_readnb(&rio_server, server_buf, MAXLINE)) > 0 && size <= MAX_OBJECT_SIZE) {
        Rio_writen(connfd, server_buf, read_bytes);
        memcpy(payload + size, server_buf, read_bytes);
        size += read_bytes;
    }

    P(&mutex);
    cache_save(uri_tmp, size, payload);
    V(&mutex);

    // Rio_readinitb(&rio_server, clientfd);

    // while ((read_bytes = Rio_readnb(&rio_server, server_buf, MAXLINE)) > 0) {
    //     Rio_writen(connfd, server_buf, read_bytes);
    // }

    Close(clientfd);
}

int main(int argc, char **argv)
{
    Sem_init(&mutex, 0, 1);
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;  /* Enough space for any address */
    char client_hostname[MAXLINE], client_port[MAXLINE];
    pthread_t tid;

    Signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }
    
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);
        printf("Connected to (%s, %s)\n", client_hostname, client_port);
        int* connfd_ptr = malloc(sizeof(int));
        *connfd_ptr = connfd;
        Pthread_create(&tid, NULL, thread, connfd_ptr);
    }
    exit(0);
}

void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    int connfd = *(int *)vargp;
    process(connfd);
    free(vargp);
    Close(connfd);
    return NULL;
}