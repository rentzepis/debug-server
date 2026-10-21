#include <stdio.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE (MAX_CACHE_SIZE / MAX_OBJECT_SIZE)
#define SBUFSIZE 16
#define NTHREADS 4


typedef struct 
{
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
} URI;


/* A circular queue to save accepted socket desciptors */
typedef struct {
    int *desciptors;
    int max_size; 
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} RequestQueue;


typedef struct
{
    char uri[MAXLINE];
    char obj[MAX_OBJECT_SIZE];
    size_t obj_size;

    int age;
    int is_empty;
    int reader_cnt;
    sem_t mutex;
    sem_t w;
} CacheLine;


typedef struct
{
    int size;
    CacheLine data[MAX_CACHE];
} Cache;


void *Worker(void *vargp);
void DoAndClose(int connfd);
void ParseUri(char *uri, URI *uri_data);
void BuildServerRequest(char *out, URI *uri_data, rio_t *client_rio);
void ClientError(int connectfd, char *msg);


void Init_request_queue(int n);
void InsertRequestQueue(int item);
int GetFromRequestQueue();


void InitCache();
size_t TryReadCache(char *url, char *obj);
void WriteCache(char *uri, char *buf, size_t size);
int GetCacheVictim();
void UpdateCacheAge();


/* global variables */
Cache cache;
RequestQueue requset_queue;


int main(int argc, char **argv)
{
    /* Setup cache, request queue, SIGPIPE handler, and thread pool. */
    InitCache();
    Init_request_queue(SBUFSIZE);    
    Signal(SIGPIPE, SIG_IGN);
    pthread_t tid;
    for (int i = 0; i < NTHREADS; i++)
    {
        Pthread_create(&tid, NULL, Worker, NULL);
    }

    int listenfd, connfd;
    socklen_t clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;
    
    if (argc != 2)
    {
        fprintf(stderr, "usage :%s <port> \n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        InsertRequestQueue(connfd);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);
    }
    return 0;
}


void *Worker(void *vargp)
{
    Pthread_detach(pthread_self());
    while (1)
    {
        int connfd = GetFromRequestQueue(&requset_queue);
        DoAndClose(connfd);
    }
}


void DoAndClose(int connfd)
{
    char buf[MAXLINE];
    char obj[MAX_OBJECT_SIZE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("%s %s %s\n", method, uri, version);
    
    /* Only GET is implemented for now */
    if (strcasecmp(method, "GET"))
    {
        ClientError(connfd, "Proxy does not implement the method\n");
        Close(connfd);
        return;
    }

    /* Check cache */
    char cache_tag[MAXLINE];
    strcpy(cache_tag, uri);
    size_t obj_len;
    if ((obj_len = TryReadCache(cache_tag, obj)) > 0)
    {
        printf("Found in cache, size: %lu\n", obj_len);
        Rio_writen(connfd, obj, obj_len);
        Close(connfd);
        return;
    }

    /* The file is not found in cache, try to connect server */
    char request[MAXLINE];
    
    URI *uri_data = (URI *) malloc(sizeof(URI));
    ParseUri(uri, uri_data);
    BuildServerRequest(request, uri_data, &rio);

    int serverfd;
    if ((serverfd = open_clientfd(uri_data->host, uri_data->port)) < 0) {
        ClientError(connfd, "Fail to connect\n");
        Close(connfd);
        return;
    }

    Rio_writen(serverfd, request, strlen(request));

    rio_t server_rio;
    int data_size = 0;
    int n = 0;

    Rio_readinitb(&server_rio, serverfd);
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
    {
        printf("proxy received %d bytes...\n", (int) n);

        if(data_size + n < MAX_OBJECT_SIZE) 
        {
            memcpy(obj + data_size, buf, n);
        }

        data_size += n;
        Rio_writen(connfd, buf, n);
    }

    // Write to local cache after closing the connect
    Close(serverfd);
    if(data_size < MAX_OBJECT_SIZE) {
        printf("Write to cache, size: %d\n", data_size);
        WriteCache(cache_tag, obj, data_size);
    }
}


void BuildServerRequest(char *out, URI *uri_data, rio_t *client_rio)
{
    char request[MAXLINE];
    char host[MAXLINE];
    char *requset_fmt = "GET %s HTTP/1.0\r\n";
    char *host_fmt = "Host: %s\r\n";
    sprintf(request, requset_fmt, uri_data->path);
    sprintf(host, host_fmt, uri_data->host, uri_data->port);
    
    char *agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    char *connect = "Connection: close\r\n";
    char *proxy_connect = "Proxy-Connection: close\r\n";

    char buf[MAXLINE];
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
    {
        if (strcmp(buf, "\r\n") == 0)
        {
            break;
        }
        else if (!strncasecmp(buf, "Host", strlen("Host")))
        {
            strcpy(host, buf);
            continue;
        }
    }

    sprintf(out, "%s%s%s%s%s\r\n", request,
                                   host,
                                   connect,
                                   proxy_connect,
                                   agent);
    return;
}


void ParseUri(char *uri, URI *uri_data)
{
    char *host_start = strstr(uri, "//");
    if (host_start != NULL) 
    {
        host_start += 2;
        uri = host_start;
    } 

    char *port_start = strstr(uri, ":");
    if (port_start != NULL)
    {
        port_start += 1;
        uri = port_start;
    }

    char *path_start = strstr(uri, "/");

    // check if requset specified port
    // host:port/path
    if (port_start != NULL)       
    {
        int port;
        sscanf(port_start, "%d%s", &port, uri_data->path);
        sprintf(uri_data->port, "%d", port);
        *(port_start - 1) = '\0';
    }
    // host/path
    else if (path_start != NULL)  
    {
        strcpy(uri_data->path, path_start);
        strcpy(uri_data->port, "80");       // http default : port 80
        *(path_start - 1) = '\0';
    }

    if (uri && uri[0] != '/')
        strcpy(uri_data->host, host_start);
    return;
}


void ClientError(int connectfd, char *msg) {
    printf("%s", msg);
    Rio_writen(connectfd, msg, strlen(msg));
}


/*************************************
 * Helper function for request queue *
 *************************************/
void Init_request_queue(int size)
{
    requset_queue.desciptors = Calloc(size, sizeof(int)); 
    requset_queue.max_size = size;
    requset_queue.front = requset_queue.rear = 0;
    Sem_init(&requset_queue.mutex, 0, 1);
    Sem_init(&requset_queue.slots, 0, size);
    Sem_init(&requset_queue.items, 0, 0);
}

void InsertRequestQueue(int fd)
{
    P(&requset_queue.slots); 
    P(&requset_queue.mutex);
    int idx = ++requset_queue.rear % requset_queue.max_size;
    requset_queue.desciptors[idx] = fd;
    V(&requset_queue.mutex);
    V(&requset_queue.items);
}


int GetFromRequestQueue()
{
    P(&requset_queue.items);
    P(&requset_queue.mutex);
    int idx = ++requset_queue.front % requset_queue.max_size;
    int fd = requset_queue.desciptors[idx];
    V(&requset_queue.mutex);
    V(&requset_queue.slots);
    return fd;
}


/*****************************
 * Helper function for cache *
 *****************************/

void InitCache()
{
    for (int i = 0; i < MAX_CACHE; i++)
    {
        cache.data[i].age = 0;
        cache.data[i].is_empty = 1;
        cache.data[i].reader_cnt = 0;
        cache.data[i].obj_size = 0;
        Sem_init(&cache.data[i].mutex, 0, 1);
        Sem_init(&cache.data[i].w, 0, 1);
    }
}


size_t TryReadCache(char *url, char *obj)
{
    for (int i = 0; i < MAX_CACHE; ++i)
    {
        P(&cache.data[i].mutex);
        cache.data[i].reader_cnt++;
        if (cache.data[i].reader_cnt == 1)
            P(&cache.data[i].w);
        V(&cache.data[i].mutex);

        // Found cache
        if (!cache.data[i].is_empty && !strcmp(url, cache.data[i].uri)) {
            size_t size = cache.data[i].obj_size;
            memcpy(obj, cache.data[i].obj, cache.data[i].obj_size);

            P(&cache.data[i].mutex);
            cache.data[i].reader_cnt--;
            if (cache.data[i].reader_cnt == 0)
                V(&cache.data[i].w);
            V(&cache.data[i].mutex);

            return size;
        }

        P(&cache.data[i].mutex);
        cache.data[i].reader_cnt--;
        if (cache.data[i].reader_cnt == 0)
            V(&cache.data[i].w);
        V(&cache.data[i].mutex);
    }

    return 0;
}


void WriteCache(char *uri, char *obj, size_t size)
{
    int i = GetCacheVictim();
    P(&cache.data[i].w);

    strcpy(cache.data[i].uri, uri);
    strcpy(cache.data[i].obj, obj);
    cache.data[i].obj_size = size;
    UpdateCacheAge();
    cache.data[i].is_empty = 0;
    cache.data[i].age = 0;

    V(&cache.data[i].w);
}


int GetCacheVictim()
{
    int max_age = 0;
    int max_idx = 0;
    for (int i = 0; i < MAX_CACHE; ++i)
    {
        P(&cache.data[i].mutex);
        cache.data[i].reader_cnt++;
        if (cache.data[i].reader_cnt == 1)
            P(&cache.data[i].w);
        V(&cache.data[i].mutex);

        /* Get a Free slot */
        if (cache.data[i].is_empty)
        {
            P(&cache.data[i].mutex);
            cache.data[i].reader_cnt--;
            if (cache.data[i].reader_cnt == 0)
                V(&cache.data[i].w);
            V(&cache.data[i].mutex);
            return i;
        }

        if (cache.data[i].age > max_age)
        {
            max_age = cache.data[i].age;
            max_idx = i;
        }

        P(&cache.data[i].mutex);
        cache.data[i].reader_cnt--;
        if (cache.data[i].reader_cnt == 0)
            V(&cache.data[i].w);
        V(&cache.data[i].mutex);
    }

    return max_idx;
}


void UpdateCacheAge()
{
    for (int i = 0; i < MAX_CACHE; i++)
    {
        if (cache.data[i].is_empty == 0)
        {
            P(&cache.data[i].w);
            cache.data[i].age++;
            V(&cache.data[i].w);
        }
    }
}

