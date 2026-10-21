#include <pthread.h>
#include <semaphore.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE  1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_OBJECT      (MAX_CACHE_SIZE / MAX_OBJECT_SIZE)

struct URI_INFO
{
    char hostname[MAXLINE];
    char port[MAXLINE];
    char filename[MAXLINE];
};

struct block
{
    char uri[MAXLINE];
    char object[MAX_OBJECT_SIZE];
    size_t size;

    int LRU;
    int is_used;

    int reader_cnt;
    sem_t mutex, w;
};

struct
{
    struct block blocks[MAX_OBJECT];
} cache;

static const char *EOL = "\r\n";
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

void doit(int fd);
int connnect_server(const char *uri, struct URI_INFO *);
void forward_requesthdrs(rio_t *client_rio, rio_t *server_rio);
void forward_response(rio_t *client_rio, rio_t *server_rio, char *uri);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

void *doit_thread(void *client_fd_);

/* cache part */
void reader_P(struct block *p)
{
    P(&p->mutex);
    p->reader_cnt++;
    if (p->reader_cnt == 1)
        P(&p->w);
    V(&p->mutex);
}

void reader_V(struct block *p)
{
    P(&p->mutex);
    p->reader_cnt--;
    if (p->reader_cnt == 0)
        V(&p->w);
    V(&p->mutex);
}

void init_cache();

// if find, return the block holding the lock
struct block *search_cache(char *uri);

void cache_object(char *uri, char *object, size_t size);

int main(int argc, char **argv)
{
    int listenfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);
    init_cache();
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen          = sizeof(clientaddr);
        int *client_fd_ptr = Malloc(sizeof(int));
        *client_fd_ptr     = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port,
                    MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, doit_thread, client_fd_ptr);
    }
}

void *doit_thread(void *client_fd_ptr)
{
    int client_fd = *(int *)client_fd_ptr;
    Pthread_detach(pthread_self());
    Free(client_fd_ptr);
    doit(client_fd);
    return NULL;
}

void doit(int client_fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t client_rio, server_rio;

    /* Read request line and headers */
    Rio_readinitb(&client_rio, client_fd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("%s %s %s\n", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(client_fd, method, "501", "Not Implemented",
                    "proxy does not implement this method");
        return;
    }

    // search from cache
    struct block *p = search_cache(uri);
    if (p) {
        fprintf(stderr, "search success\n");
        for (Rio_readlineb(&client_rio, buf, MAXLINE); strcmp(buf, EOL);
             Rio_readlineb(&client_rio, buf, MAXLINE))
            ;
        Rio_writen(client_fd, p->object, p->size);

        P(&p->mutex);
        p->LRU++;
        p->reader_cnt--;
        if (p->reader_cnt == 0)
            V(&p->w);
        V(&p->mutex);

        Close(client_fd);
        return;
    }

    /* Open Server Connection */
    struct URI_INFO uri_info;
    int server_fd = connnect_server(uri, &uri_info);
    if (server_fd < 0)
        return;
    Rio_readinitb(&server_rio, server_fd);

    /* requst line */
    sprintf(buf, "GET /%s HT\rTP/1.0\r\n", uri_info.filename);
    Rio_writen(server_fd, buf, strlen(buf));
    /* Host */
    sprintf(buf, "Host: %s\r\n", uri_info.hostname);
    Rio_writen(server_fd, buf, strlen(buf));
    /* request header */
    forward_requesthdrs(&client_rio, &server_rio);
    puts("");
    /* response */
    forward_response(&client_rio, &server_rio, uri);

    Close(server_fd);
    Close(client_fd);
}

int connnect_server(const char *_uri, struct URI_INFO *uri_info)
{
    char uri[MAXLINE];
    strcpy(uri, _uri);

    char *filename = NULL;
    char *hostname = NULL, *port = NULL;

    char *p  = strstr(uri, "//");
    hostname = p ? p + 2 : uri;
    p        = index(hostname, ':');
    if (p) {
        *p   = '\0';
        port = p + 1;
        if ((p = index(port, '/')))
            *p = '\0';
        filename = port + strlen(port) + 1;
    } else {
        port     = "80";
        p        = index(hostname, '/');
        *p       = '\0';
        filename = p + 1;
    }

    strcpy(uri_info->hostname, hostname);
    strcpy(uri_info->port, port);
    strcpy(uri_info->filename, filename);

    printf("hostname: %s, port: %s, filename: %s\n", hostname, port, filename);
    return open_clientfd(hostname, port);
}

void forward_requesthdrs(rio_t *client_rio, rio_t *server_rio)
{
    char buf[MAXLINE];

    for (Rio_readlineb(client_rio, buf, MAXLINE); strcmp(buf, EOL);
         Rio_readlineb(client_rio, buf, MAXLINE)) {
        Rio_writen(server_rio->rio_fd, buf, strlen(buf));
    }

    Rio_writen(server_rio->rio_fd, user_agent_hdr, strlen(user_agent_hdr));
    Rio_writen(server_rio->rio_fd, connection_hdr, strlen(connection_hdr));
    Rio_writen(server_rio->rio_fd, proxy_conn_hdr, strlen(proxy_conn_hdr));
    Rio_writen(server_rio->rio_fd, EOL, 2);
    return;
}

void forward_response(rio_t *client_rio, rio_t *server_rio, char *uri)
{
    int object_size = 0;
    char object[MAX_OBJECT_SIZE];

    size_t len;
    char buf[MAXLINE];

    while ((len = Rio_readlineb(server_rio, buf, MAXLINE))) {
        printf("%s", buf);
        Rio_writen(client_rio->rio_fd, buf, len);

        if (object_size + len < MAX_OBJECT_SIZE) {
            memcpy(object + object_size, buf, len);
            object_size += len;
        } else {
            object_size = MAX_OBJECT_SIZE + 1;
        }
    }

    if (object_size <= MAX_OBJECT_SIZE) {
        cache_object(uri, object, object_size);
    }

    printf("Connection closed by foreign host.\n");
    return;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg)
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
    sprintf(buf, "<body bgcolor=ffffff>\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

// if find, return the block holding the lock
struct block *search_cache(char *uri)
{
    fprintf(stderr, "searching cache for uri: %s\n", uri);
    for (int i = 0; i < MAX_OBJECT; i++) {
        struct block *p = &cache.blocks[i];

        // readers hold w during reading
        reader_P(p);

        if (p->is_used && strcmp(p->uri, uri) == 0) {
            return p;
        }

        reader_V(p);
    }
    return NULL;
}

void init_cache()
{
    for (int i = 0; i < MAX_OBJECT; i++) {

        struct block *p = &cache.blocks[i];
        p->is_used = p->reader_cnt = 0;
        sem_init(&p->mutex, 0, 1);
        sem_init(&p->w, 0, 1);
    }
}

void cache_object(char *uri, char *object, size_t size)
{
    fprintf(stderr, "caching object from uri %s, with size %ld\n", uri, size);
    int minLRU = 9999;
    struct block *write_ptr;

    // find a slot
    for (int i = 0; i < MAX_OBJECT; i++) {
        struct block *p = &cache.blocks[i];
        reader_P(p);
        if (p->is_used == 0) {
            write_ptr = p;
            reader_V(p);
            break;
        }
        if (p->LRU < minLRU) {
            minLRU    = p->LRU;
            write_ptr = p;
        }
        reader_V(p);
    }

    P(&write_ptr->w);

    write_ptr->LRU     = 0;
    write_ptr->is_used = 1;
    write_ptr->size    = size;
    strcpy(write_ptr->uri, uri);
    memcpy(write_ptr->object, object, size);

    V(&write_ptr->w);
}
