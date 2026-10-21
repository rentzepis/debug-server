#include <bits/posix2_lim.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <signal.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAXLINE 8192

#define SBUFSIZE 16
#define NTHREADS 6

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void SIGPIPE_handler(int sig)
{
    fprintf(stderr, "SIGPIPE caught\n");
    return;
}

typedef struct URI
{
    char hostname[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
}tURI;

typedef struct 
{
    char buf[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int valid;
    int LRU;

    int read_cnt;
    sem_t mutex;
    sem_t w;
} tCacheline;

typedef struct
{
    tCacheline data[MAXLINE];
    int num;
} tCache;

int LRU_cnt = 0;


void build_header(char *header, tURI *pURI, rio_t *client_rio)
{
    strcpy(header, "GET ");
    strcat(header, pURI->path);
    strcat(header, " HTTP/1.1\r\n");

    strcat(header, "Host: ");
    strcat(header, pURI->hostname);
    if(strcmp(pURI->port, "80"))
    {
        strcat(header, ":");
        strcat(header, pURI->port);
    }
    strcat(header, "\r\n");

    strcat(header, user_agent_hdr);

    char buf[MAXLINE];
    while(Rio_readlineb(client_rio, buf, MAXLINE) > 0)
    {
        if(strcmp(buf, "\r\n") == 0)
            break;
        if(strstr(buf, "Connection") || strstr(buf, "Proxy-Connection") )
        {
            strcat(header, buf);
        }
    }

    strcat(header, "\r\n");

    // printf("DEBUGGGGGGGGGG:!! %s\n", header);
}

void parse_uri(char *uri, tURI *pURI)
{
    char *ptr = strstr(uri, "http://");
    if(ptr == NULL)
    {
        fprintf(stderr, "Invalid URI\n");
        return;
    }
    ptr += strlen("http://");

    char *ptr2 = strstr(ptr, ":");
    if(ptr2 == NULL)
    {
        strcpy(pURI->port, "80");
    }
    else
    {
        char *ptr3 = strstr(ptr2, "/");
        if(ptr3 == NULL)
        {
            fprintf(stderr, "Invalid URI\n");
            return;
        }
        *ptr3 = '\0';
        sscanf(ptr2, ":%s", pURI->port);
        *ptr3 = '/';
    }

    ptr2 = strstr(ptr2, "/");
    if(ptr2 == NULL)
    {
        fprintf(stderr, "Invalid URI\n");
        return;
    }
    
    sscanf(ptr2, "%s", pURI->path);
    
    ptr2 = strstr(ptr, ":");
    if(ptr2 == NULL)
    {
        sscanf(ptr, "%[^/]", pURI->hostname);
    }
    else
    {
        sscanf(ptr, "%[^:]", pURI->hostname);
    }

}

tCache *init_cache()
{
    tCache *cache = (tCache *)malloc(sizeof(tCache));
    cache->num = 0;
    return cache;
}

int get_cache(tCache *cache, char *uri)
{
    for(int i = 0; i < cache->num; i++)
    {
        if(strcmp(cache->data[i].uri, uri) == 0)
        {
            cache->data[i].LRU = (++LRU_cnt);
            return i;
        }
    }
    return -1;
}

void write_cache(tCache *cache, char *uri, char *buf)
{
    if(cache->num == MAXLINE)
    {
        for(int i = 0; i < cache->num; i++)
        {
            if(cache->data[i].LRU == LRU_cnt && cache->data[i].valid == 1)
            {
                cache->data[i].valid = 0;
                break;
            }
        }
    }
    strcpy(cache->data[cache->num].uri, uri);
    strcpy(cache->data[cache->num].buf, buf);
    cache->data[cache->num].valid = 1;
    cache->data[cache->num].LRU = (++LRU_cnt);
    cache->data[cache->num].read_cnt = 0;
    Sem_init(&cache->data[cache->num].mutex, 0, 1);
    Sem_init(&cache->data[cache->num].w, 0, 1);
    cache->num++;
}

void free_cache(tCache *cache)
{
    for(int i = 0; i < cache->num; i++)
    {
        Free(cache->data[i].buf);
    }
    Free(cache);
}

tCache *cache = NULL;

void doit(int connfd)
{
    rio_t rio, server_rio;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    char server_request[MAXLINE];

    char cache_tag[MAXLINE];
    
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);

    sscanf(buf, "%s %s %s", method, uri, version);

    strcpy(cache_tag, uri);

    //printf("DOIT: m u v!!: %s %s %s\n", method, uri, version);

    if(strcasecmp(method, "GET"))
    {
        fprintf(stderr, "Only GET is allowed\n");
        return;
    }

    tURI *pURI = (tURI *)malloc(sizeof(tURI));

    int index = get_cache(cache, cache_tag);
    if(index != -1) // cache hit :)
    {
        P(&cache->data[index].mutex);
        cache->data[index].read_cnt++;
        if(cache->data[index].read_cnt == 1)
        {
            P(&cache->data[index].w);
        }
        V(&cache->data[index].mutex);

        Rio_writen(connfd, cache->data[index].buf, strlen(cache->data[index].buf));

        P(&cache->data[index].mutex);
        cache->data[index].read_cnt--;
        if(cache->data[index].read_cnt == 0)
        {
            V(&cache->data[index].w);
        }
        V(&cache->data[index].mutex);
        return ;
    }

    parse_uri(uri, pURI);

    //printf("pURI is : %s %s %s\n", pURI->hostname, pURI->port, pURI->path);


    build_header(server_request, pURI, &rio);

    printf("server_request is : %s\n", server_request);

    int serverfd = Open_clientfd(pURI->hostname, pURI->port);

    //printf("DE: client ok\n");

    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, server_request, strlen(server_request));

    int buf_size = 0;
    char cache_buf[MAX_OBJECT_SIZE];

    size_t n;
    while((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0)
    {
        buf_size += n;
        if(buf_size < MAX_OBJECT_SIZE)
        {
            strcat(cache_buf, buf);
        }
        Rio_writen(connfd, buf, n);
    }

    //printf("OKKKKKKKKKKKK\n");

    Free(pURI);
    close(serverfd);

    if(buf_size < MAX_OBJECT_SIZE)
    {
        write_cache(cache, cache_tag, cache_buf);
    }
}

typedef struct
{
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear)%(sp->n)] = item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[(++sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

sbuf_t sbuf;

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while(1)
    {
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        close(connfd);
    }
}


int main(int argc, char **argv) 
{
    if(argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    cache = init_cache();

    int listenfd, connfd;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;

    signal(SIGPIPE, SIGPIPE_handler);

    listenfd = Open_listenfd(argv[1]);

    sbuf_init(&sbuf, 16);


    pthread_t tid;
    for(int i = 0; i < NTHREADS; i++)
    {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    while(1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

    }
    return 0;
}