#include "csapp.h"
#include <sys/socket.h>
#include <netdb.h>

#define WEB_PREFIX "http://"
#define NTHREADS 8

#define MAX_CACHE_SIZE (1024 * 1024) // 1 MiB in bytes
#define MAX_OBJECT_SIZE 1024         // 1 KiB in bytes
#define CACHE_BLOCK_NUM (MAX_CACHE_SIZE / MAX_OBJECT_SIZE)

typedef struct
{
    char object[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int LRU;
    int is_empty;

    int read_cnt;
    sem_t read_mutex;
    sem_t write_mutex;
} block;

block cache[CACHE_BLOCK_NUM + 1];

void handle_request(int connfd);
void parse_uri(char *uri, char *filename, char *host, char *port);
void read_requesthdrs(rio_t *rp);
void forward_request(char *method, char *host, char *port, char *filename, int connfd, block *cache_block);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void *thread(void *vargp);

int main(int argc, char *argv[])
{

    int listenfd;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // init cache
    for (int i = 0; i < CACHE_BLOCK_NUM; i++)
    {
        cache[i].is_empty = 1;
        cache[i].LRU = 0;
        cache[i].read_cnt = 0;
        Sem_init(&cache[i].read_mutex, 0, 1);
        Sem_init(&cache[i].write_mutex, 0, 1);
    }

    pthread_t tid[NTHREADS];

    listenfd = Open_listenfd(argv[1]);

    while (1)
    {
        struct sockaddr_in clientaddr;
        socklen_t clientlen = sizeof(clientaddr);
        int connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0)
        {
            perror("accept");
            continue;
        }

        pthread_create(&tid, NULL, thread, (void *)&connfd);
    }
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());

    handle_request(connfd);

    close(connfd);

    return NULL;
}
void handle_request(int connfd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE];
    char filename[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))
        return;

    // replace HTTP/1.1 with HTTP/1.0
    char *pos = NULL;
    if ((pos = strstr(buf, "HTTP/1.1")) != NULL)
    {
        buf[pos - buf + strlen("HTTP/1.1") - 1] = '0';
    }
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET"))
        unix_error("Proxy does not implement this method");

    // check cache
    char cache_uri[MAXLINE];
    strcpy(cache_uri, uri);

    int i;
    if ((i = get_Cache(cache_uri)) != -1)
    {
        // 加锁
        // P(&cache[i].read_mutex);
        // cache[i].read_cnt++;
        // if (cache[i].read_cnt == 1)
        //     P(&cache[i].write_mutex);
        // V(&cache[i].read_mutex);

        Rio_writen(connfd, cache[i].object, strlen(cache[i].object));

        // 解锁
        // P(&cache[i].read_mutex);
        // cache[i].read_cnt--;
        // if (cache[i].read_cnt == 0)
        //     V(&cache[i].write_mutex);
        // V(&cache[i].read_mutex);

        return;
    }

    // cache miss

    block *cache_block = malloc(sizeof(block));
    cache_block->is_empty = 0;
    cache_block->LRU = 0;
    strcpy(cache_block->uri, cache_uri);

    read_requesthdrs(&rio);

    parse_uri(uri, filename, host, port);

    forward_request(method, host, port, filename, connfd, cache_block);

    for (int i = 0; i < CACHE_BLOCK_NUM; i++)
    {
        if (cache[i].is_empty == 1)
        {
            cache[i] = *cache_block;
            break;
        }
    }

    return;
}

void forward_request(char *method, char *host, char *port, char *filename, int connfd, block *cache_block)
{
    static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    static const char *connection_hdr = "Connection: close\r\n";
    static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

    char buf[MAXLINE];
    char request[MAXLINE];

    sprintf(request, "%s %s HTTP/1.0\r\n", method, filename);
    sprintf(buf, "Host: %s\r\n", host);
    strcat(request, buf);
    sprintf(buf, "Filename: %s\r\n", filename);
    strcat(request, buf);
    sprintf(buf, "Method: %s\r\n", method);
    strcat(request, buf);
    strcat(request, user_agent_hdr);
    strcat(request, connection_hdr);
    strcat(request, proxy_connection_hdr);
    strcat(request, "\r\n");

    printf("Forwarding request:\n");
    printf("%s", request);

    //  splite localhost:223
    char *hostname = strtok(host, ":");

    int clientfd = Open_clientfd(hostname, port);

    rio_t rio;
    Rio_readinitb(&rio, clientfd);
    Rio_writen(rio.rio_fd, request, strlen(request));
    int size_buf = 0;

    int n;
    int total_size = 0;
    char cache_buf[MAX_OBJECT_SIZE];
    char tinyResponse[MAXLINE];
    while ((n = Rio_readlineb(&rio, tinyResponse, MAXLINE)) != 0)
    {
        total_size += n;
        if (total_size < MAX_OBJECT_SIZE)
        {
            strcat(cache_buf, tinyResponse);
        }
        Rio_writen(connfd, tinyResponse, n);
    }
    Close(clientfd);
    if (total_size < MAX_OBJECT_SIZE)
    {
        strcpy(cache_block->object, cache_buf);
    }
    else
    {
        cache_block->is_empty = 1;
    }
}

void parse_uri(char *uri, char *filename, char *host, char *port)
{
    char *hostp = strstr(uri, WEB_PREFIX) + strlen(WEB_PREFIX);
    char *slash = strstr(hostp, "/");
    char *colon = strstr(hostp, ":");
    // get host name
    strncpy(host, hostp, slash - hostp);
    // get port number
    strncpy(port, colon + 1, slash - colon - 1);
    // get file name
    strcpy(filename, slash);
}

void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while (strcmp(buf, "\r\n"))
    { // line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client */
    get_filetype(filename, filetype);
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n", filesize);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
    Rio_writen(fd, buf, strlen(buf));

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
    Close(srcfd);
    Rio_writen(fd, srcp, filesize);
    Munmap(srcp, filesize);
}

void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}

int get_Cache(char *name)
{
    for (int i = 0; i < CACHE_BLOCK_NUM; i++)
    {
        if (cache[i].is_empty == 0 && strcmp(cache[i].uri, name) == 0)
        {
            return i;
        }
    }
    return -1;
}
