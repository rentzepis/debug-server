#include <stdio.h>
#include <semaphore.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CacheBlock_{
    char request[MAXLINE];
    char cache[MAX_OBJECT_SIZE];
    size_t size;
    struct CacheBlock_ *next;
} CacheBlock;

typedef struct {
    size_t cacheSize;
    CacheBlock *head;
    sem_t mutex;
} Cache;

Cache cache;

void init_cache() {
    cache.cacheSize = 0;
    cache.head = NULL;
}

void free_cache() {
    CacheBlock *block = cache.head, *next;
    while (block != NULL) {
        next = block->next;
        free(block);
        block = next;
    }
}

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int startsWith(char *s, char* prefix) {
    for (; *s && *prefix; s++, prefix++) if (*s != *prefix) return 0;
    return *prefix == 0;
}
void do_proxy_thread(void *);

int main(int argc, char **argv)
{
    struct sockaddr_storage *clientAddr = NULL;
    int addrSize = sizeof(clientAddr);
    
    int listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        int tid;
        int connectfd = Accept(listenfd, (SA*) clientAddr, &addrSize);
        int *argv = malloc(sizeof(int));
        *argv = connectfd;
        pthread_create(&tid, NULL, do_proxy_thread, argv);
    }
    
    return 0;
}

void do_proxy_thread(void *argv) {
    int fd = *((int *)argv);
    free(argv);

    char buffer[MAXLINE];
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];
    rio_t connectRio;
    rio_t *connectRp = &connectRio;
    Rio_readinitb(connectRp, fd);

    Rio_readlineb(connectRp, buffer, MAXBUF);
    sscanf(buffer, "%s %s %s", method, url, version);

    CacheBlock *block = cache.head;
    while (block != NULL)
    {
        if (strcmp(block->request, buffer) == 0) {
            Rio_writen(fd, block->cache, block->size);
            Close(fd);
            return;
        }
        block = block->next;
    }
    
    char host[50], path[100], hostname[50], port[6];
    if (startsWith(url, "http://")) {
        sscanf(url, "%*[^:]://%49[^/]/%99s", host, path);
    } else {
        sscanf(url, "%49[^/]/%99s", host, path);
    }
    
    printf("host: %s\npath: %s\n", host, path);
    int n = sscanf(host, "%49[^:]:%5s", hostname, port);
    
    int client_fd; 
    if (n == 0) return;
    else if (n == 1) {
        client_fd = Open_clientfd(hostname, "80");
    } else {
        client_fd = Open_clientfd(hostname, port);
    }

    char request[BUFSIZ];
    sprintf(request, "%s /%s HTTP/1.0\r\n\r\n", method, path);
    sprintf(request, "%shost: %s\r\n", request, hostname);
    sprintf(request, "%sUser-Agent: %s", request, user_agent_hdr);
    sprintf(request, "%sConnection: close\r\nProxy-Connection: close\r\n\r\n", request);
    Rio_writen(client_fd, request, strlen(request));

    block = cache.head;
    if (block == NULL) {
        cache.head = block = malloc(sizeof(CacheBlock));
    } else {
        while (block->next != NULL) {
            block = block->next;
        }
        block->next = malloc(sizeof(CacheBlock));
        block = block->next;
    }

    rio_t clientRio;
    rio_t *clientRp = &clientRio;
    Rio_readinitb(clientRp, client_fd);
    strncpy(block->request, buffer, sizeof(block->request));
    while ((n = Rio_readnb(clientRp, buffer, BUFSIZ)) != 0)
    { 
        Rio_writen(fd, buffer, n);
        memcpy(block->cache+block->size, buffer, n);
        block->size += n;
    }
    Close(fd);
    Close(client_fd);
}
