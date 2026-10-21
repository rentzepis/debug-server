#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16

typedef struct CachedItem CachedItem;

struct CachedItem {
    char cache_object[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    clock_t access_time;
    size_t size;
    struct CachedItem *prev;
    struct CachedItem *next;
};

typedef struct {
    struct CachedItem *head;
    struct CachedItem *tail;
} CacheList;

CacheList list;
size_t cache_size = 0;
int read_cnt;

sbuf_t sbuf;    /* Shared buffer of connected descriptors */
sem_t w_mutex, r_mutex, u_mutex;

void doit(int fd);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void parse_header(char *http_header, char *hostname, char *path, rio_t *client_rio);
int check_header(char *buf);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";
static const char *request_header = "GET %s HTTP/1.0\r\n";
static const char headers[3][30] = {
    "Connection",
    "Proxy-Connection",
    "User-Agent"
};

void cache_init();
int check_cache(char *uri, int fd);
void cache_URL(char *URL, char *object, size_t size);
void evict();
CachedItem *find(char *URL);


/*
 * Part 1: Figure 11.29 The TINY Web server (`code/netp/tiny/tiny.c`)
 * Part 2: Figure 12.28 A prethreaded concurent echo server (`code/conc/echoservert-pre.c`)
 */ 
int main(int argc, char *argv[])
{
    int i, listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    sbuf_init(&sbuf, SBUFSIZE);
    list.head = malloc(sizeof(CachedItem));
    cache_init();

    for (i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);   /* Create worker threads */
    }

    while(1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);

        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s)\n", hostname, port);

        sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
    }    
    return 0;
}


/*
 * Part 2: Figure 12.28 A prethreaded concurent echo server (`code/conc/echoservert-pre.c`)
 */ 
void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);    /* Remove connfd from buffer */
        doit(connfd);                   /* Service client */
        Close(connfd);
    }
}


/*
 * Part 1: Figure 11.30 TINY `doit` handles one HTTP transaction (`code/netp/tiny/tiny.c`)
 */
void doit(int fd)
{
    int clientfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char http_header[MAXLINE];
    rio_t client_rio, server_rio;

    Rio_readinitb(&client_rio, fd);
    Rio_readlineb(&client_rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    // if method is not GET
    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method");
        return;
    }

    if (check_cache(uri, fd)) {
        return;
    }

    parse_uri(uri, hostname, path, port);

    parse_header(http_header, hostname, path, &client_rio);

    clientfd = Open_clientfd(hostname, port);

    if(clientfd < 0){
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio, clientfd);
    Rio_writen(clientfd, http_header, strlen(http_header));

    char obj_buf[MAX_OBJECT_SIZE];
    size_t n, obj_buf_size = 0;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        Rio_writen(fd, buf, n);
        obj_buf_size += n;
        if (obj_buf_size < MAX_OBJECT_SIZE) {
            strncat(obj_buf, buf, strlen(buf));
        }
    }

    Close(clientfd);

    if (obj_buf_size < MAX_OBJECT_SIZE) {
        cache_URL(uri, obj_buf, obj_buf_size);   
    }

    return;
}


int check_cache(char *uri, int fd)
{
    int flag = 0;

    P(&r_mutex);
    read_cnt++;
    if (read_cnt == 1) //first in 
		P(&w_mutex);
	V(&r_mutex);

    CachedItem * cached_item = find(uri);
    
    if (cached_item) {
        P(&u_mutex);
        rio_writen(fd, cached_item->cache_object, cached_item->size);
        cached_item->access_time = clock();
        V(&u_mutex);   
        flag = 1;
        V(&u_mutex);
    }

    P(&r_mutex);
	read_cnt--;
	if (read_cnt == 0) //last out
		V(&w_mutex);
	V(&r_mutex);

    return flag;
}


void parse_uri(char *uri, char *hostname, char *path, char *port)
{
    char *start_ptr = strstr(uri, "http://");
    start_ptr = start_ptr ? start_ptr + strlen("http://") : uri;
    char *port_ptr = strstr(start_ptr, ":");

    if (port_ptr) {
        sscanf(start_ptr, "%[^:]:%[^/]%s", hostname, port, path);
    } else {
        sscanf("80", "%s", port);
        sscanf(start_ptr, "%[^/]%s", hostname, path);
    }
}


void parse_header(char *http_header, char *hostname, char *path, rio_t *client_rio)
{
    char buf[MAXLINE];
    char header_request[MAXLINE], header_host[MAXLINE], header_other[MAXLINE];

    sprintf(header_request, request_header, path);

    Rio_readlineb(client_rio, buf, MAXLINE);
    while (strncmp(buf, "\r\n", strlen("\r\n"))) {
        if (!strncmp(buf, "Host", strlen("Host"))) {
            strncpy(header_host, buf, strlen(buf));
            Rio_readlineb(client_rio, buf, MAXLINE);
            continue;
        }

        if (!check_header(buf)) {
            strncat(header_other, buf, strlen(buf));
        }

        Rio_readlineb(client_rio, buf, MAXLINE);
    }

    if (strlen(header_host) == 0) {
        sprintf(header_host, "Host: %s\r\n", hostname);
    }

    sprintf(http_header, "%s%s%s%s%s%s%s",
            header_request,
            header_host,
            user_agent_hdr,
            conn_hdr,
            proxy_conn_hdr,
            header_other,
            "\r\n");
}


int check_header(char *buf) {
    int i;
    char *ptr = strstr(buf, ":");
    int hdr_len = strlen(buf) - strlen(ptr);
    char *hdr = malloc(hdr_len);
    strncpy(hdr, buf, hdr_len);
    for (i = 0; i < 3; i++) {
        if (!strncmp(hdr, headers[i], hdr_len)) {
            return 1;
        }
    }
    return 0;
}


void cache_init()
{
    strcpy(list.head->cache_object, "");
    strcpy(list.head->uri, "");
    list.head->access_time = clock();
    list.head->size = 0;
    list.head->prev = NULL;
    list.head->next = NULL;
    Sem_init(&w_mutex, 0, 1);
    Sem_init(&r_mutex, 0, 1);
    Sem_init(&u_mutex, 0, 1);
    list.tail = list.head;
    cache_size = 0;
}


void cache_URL(char *URL, char *object, size_t size)
{
    P(&w_mutex);

    if ((cache_size + size) > MAX_CACHE_SIZE) {
        while((cache_size + size) > MAX_CACHE_SIZE) {
            evict();
        }
    }

    CachedItem *new_item = malloc(sizeof(CachedItem));
    strcpy(new_item->cache_object, object);
    strcpy(new_item->uri, URL);
    new_item->access_time = clock();
    new_item->size = size;
    new_item->prev = list.tail;
    new_item->next = NULL;
    list.tail->next = new_item;
    list.tail = new_item;
    cache_size += size;

    V(&w_mutex);
}


void evict()
{
    CachedItem * temp = list.head;
    temp = temp->next;
    CachedItem * evict_item = temp;
    clock_t oldest_access = temp->access_time;
    while(temp) {
        if (oldest_access > temp->access_time) {
            oldest_access = temp->access_time;
            evict_item = temp;
        }
        temp = temp->next;
    }

    if (!evict_item->next) {
        list.tail = evict_item->prev;
    }
    if (!evict_item->prev) {
        list.head = evict_item->next;
    }
    evict_item->prev->next = evict_item->next;
    evict_item->next->prev = evict_item->prev;

    free(evict_item->cache_object);
    free(evict_item->uri);
    free(evict_item);
    cache_size -= evict_item->size;
}


CachedItem *find(char *URL)
{   
    CachedItem *temp = list.head;
    while (temp != NULL) {
        if (!strcmp(URL, temp->uri)) {  // found matching cache line
            return temp;
        }
        temp = temp->next;
    }
    return temp;
}
