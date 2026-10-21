#include <stdio.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CAHCE_NUM 10

typedef struct
{
    char object[MAX_OBJECT_SIZE];
    char url[MAXLINE];
    int time;
} cache_line_t;

typedef struct
{
    cache_line_t data[MAX_CAHCE_NUM];
    int num;
} cache_t;

cache_t cache;
int readcnt;
int current_time;
sem_t mutex;
sem_t w;

void init_cache();
int read_cache(char *url, char *object);
void write_cache(char *url, char *object);
void *thread(void *vargp);
void doit(int connfd);
void parse_url(char *url, char *hostname, char *port, char *filename);
void parse_header(rio_t *rio, char *host, char *header);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, *connfd;
    socklen_t clientlen;
    pthread_t tid;
    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    Signal(SIGPIPE, SIG_IGN);
    init_cache();
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfd);
    }
    return 0;
}

void *thread(void *vargp)
{
    int connfd = *(int *)vargp;
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int client_fd)
{
    char buf[MAXLINE];
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], path[MAXLINE];
    char header[MAXLINE];
    char cache_url[MAXLINE];
    char cache_object[MAX_OBJECT_SIZE];
    int server_fd;
    rio_t client_rio, server_rio;
    Rio_readinitb(&client_rio, client_fd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);
    if (strcasecmp(method, "GET"))
    {
        clienterror(client_fd, method, "501", "Not implemented", "Proxy dose not implement this method\n");
        fprintf(stderr, "%s: Proxy dose not implement this method\n", method);
        return;
    }
    if (read_cache(url, cache_object))
    {
        Rio_writen(client_fd, cache_object, strlen(cache_object));
        return;
    }
    strcpy(cache_url, url);
    parse_url(url, host, port, path);
    parse_header(&client_rio, host, header);
    server_fd = Open_clientfd(host, port);
    Rio_readinitb(&server_rio, server_fd);
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    Rio_writen(server_fd, buf, strlen(buf));
    Rio_writen(server_fd, header, strlen(header));
    int total = 0, n = 0;
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) != 0)
    {
        if (total + n < MAX_OBJECT_SIZE)
            memcpy(cache_object + total, buf, n);
        total += n;
        Rio_writen(client_fd, buf, n);
    }
    if (total < MAX_OBJECT_SIZE)
        write_cache(cache_url, cache_object);
    close(server_fd);
}

void parse_url(char *url, char *host, char *port, char *path)
{
    char *hostpose = strstr(url, "//");
    if (hostpose == NULL)
    {
        char *pathpose = strstr(url, "/");
        if (pathpose != NULL)
            strcpy(path, pathpose);
        strcpy(port, "80");
    }
    else
    {
        char *portpose = strstr(hostpose + 2, ":");
        if (portpose != NULL)
        {
            int portnum;
            sscanf(portpose + 1, "%d%s", &portnum, path);
            sprintf(port, "%d", portnum);
            *portpose = '\0';
        }
        else
        {
            char *pathpose = strstr(hostpose + 2, "/");
            if (pathpose != NULL)
            {
                strcpy(path, pathpose);
                strcpy(port, "80");
                *pathpose = '\0';
            }
        }
        strcpy(host, hostpose + 2);
    }
}

void parse_header(rio_t *rio, char *host, char *header)
{
    char buf[MAXLINE];
    int has_host_flag = 0;
    while (1)
    {
        rio_readlineb(rio, buf, MAXLINE);
        if (strcmp(buf, "\r\n") == 0)
            break;
        if (!strncasecmp(buf, "Connection:", strlen("Connection:")))
            continue;
        if (!strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")))
            continue;
        if (!strncasecmp(buf, "User-Agent:", strlen("User-Agent:")))
            continue;
        if (!strncasecmp(buf, "Host:", strlen("Host:")))
            has_host_flag = 1;
        strcat(header, buf);
    }
    if (!has_host_flag)
    {
        sprintf(buf, "Host: %s\r\n", host);
        strcat(header, buf);
    }
    strcat(header, "Connection: close\r\n");
    strcat(header, "Proxy-Connection: close\r\n");
    strcat(header, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n");
    strcat(header, "\r\n");
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];
    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));
    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

void init_cache()
{
    current_time = 0;
    readcnt = 0;
    cache.num = 0;
    sem_init(&mutex, 0, 1);
    sem_init(&w, 0, 1);
}

int read_cache(char *url, char *object)
{
    P(&mutex);
    readcnt++;
    if (readcnt == 1)
        P(&w);
    V(&mutex);
    int hit_flag = 0;
    for (int i = 0; i < cache.num; i++)
    {
        if (!strcmp(cache.data[i].url, url))
        {
            P(&mutex);
            cache.data[i].time = current_time++;
            V(&mutex);
            strcpy(object, cache.data[i].object);
            hit_flag = 1;
            break;
        }
    }
    P(&mutex);
    readcnt--;
    if (readcnt == 0)
        V(&w);
    V(&mutex);
    return hit_flag;
}

void write_cache(char *url, char *object)
{
    P(&w);
    if (cache.num == MAX_CAHCE_NUM)
    {
        int lru_idx;
        int lru_time = current_time;
        for (int i = 0; i < MAX_CAHCE_NUM; i++)
        {
            if (cache.data[i].time < lru_time)
            {
                lru_time = cache.data[i].time;
                lru_idx = i;
            }
        }
        strcpy(cache.data[lru_idx].url, url);
        strcpy(cache.data[lru_idx].object, object);
        cache.data[lru_idx].time = current_time++;
    }
    else
    {
        strcpy(cache.data[cache.num].url, url);
        strcpy(cache.data[cache.num].object, object);
        cache.data[cache.num].time = current_time++;
        cache.num++;
    }
    V(&w);
}
