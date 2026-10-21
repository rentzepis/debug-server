#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct cache_entry {
    char uri[MAXLINE];
    char *data;
    int size;
    struct cache_entry *prev;
    struct cache_entry *next;
} cache_entry_t;

typedef struct {
    cache_entry_t *head;
    cache_entry_t *tail;
    int total_size;
    sem_t lock;
} cache_t;

static cache_t cache;

void do_request(int clientfd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void sigpipe_handler(int sig);
void *thread(void *vargp);

void cache_init(cache_t *cache);
int cache_get(cache_t *cache, const char *uri, char **data, int *size);
void cache_insert(cache_t *cache, const char *uri, const char *data, int size);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, sigpipe_handler);

    listenfd = Open_listenfd(argv[1]);

    cache_init(&cache);

    while (1) {
        int *connfdp;
        pthread_t tid;

        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        Getnameinfo((SA *)&clientaddr, clientlen,
                    hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        connfdp = Malloc(sizeof(int));
        *connfdp = connfd;
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);

    Pthread_detach(pthread_self());
    Free(vargp);

    do_request(connfd);
    Close(connfd);
    return NULL;
}

void sigpipe_handler(int sig)
{
    return;
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
    char *hostbegin;
    char *portbegin;
    char *pathbegin;
    char uri_copy[MAXLINE];

    strcpy(uri_copy, uri);

    strcpy(port, "80");

    hostbegin = strstr(uri_copy, "//");
    if (hostbegin)
        hostbegin += 2;
    else
        hostbegin = uri_copy;

    pathbegin = strchr(hostbegin, '/');
    if (pathbegin) {
        strcpy(path, pathbegin);
        *pathbegin = '\0';
    } else {
        strcpy(path, "/");
    }

    portbegin = strchr(hostbegin, ':');
    if (portbegin) {
        *portbegin = '\0';
        strcpy(port, portbegin + 1);
    }

    strcpy(hostname, hostbegin);
}

void do_request(int clientfd)
{
    char buf[MAXLINE];
    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];
    char hostname[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
    char headers[MAXLINE * 8];
    rio_t client_rio;
    rio_t server_rio;
    int serverfd;
    ssize_t n;
    int has_host = 0;
    char *cached_data = NULL;
    int cached_size = 0;

    Rio_readinitb(&client_rio, clientfd);
    if (Rio_readlineb(&client_rio, buf, MAXLINE) <= 0)
        return;

    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        clienterror(clientfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    parse_uri(uri, hostname, port, path);

    snprintf(headers, sizeof(headers), "GET %s HTTP/1.0\r\n", path);

    while (1) {
        if (Rio_readlineb(&client_rio, buf, MAXLINE) <= 0)
            break;

        if (strcmp(buf, "\r\n") == 0)
            break;

        if (strncasecmp(buf, "Host:", 5) == 0) {
            has_host = 1;
            strcat(headers, buf);
        }
        else if (strncasecmp(buf, "User-Agent:", 11) == 0) {
        }
        else if (strncasecmp(buf, "Connection:", 11) == 0) {
        }
        else if (strncasecmp(buf, "Proxy-Connection:", 17) == 0) {
        }
        else {
            strcat(headers, buf);
        }
    }

    if (!has_host) {
        size_t used = strlen(headers);
        size_t avail = sizeof(headers) - used;
        if (avail > 0) {
            int written = snprintf(headers + used, avail, "Host: %s\r\n", hostname);
            if (written < 0 || (size_t)written >= avail) {
                headers[sizeof(headers) - 1] = '\0';
            }
        }
    }

    strcat(headers, (char *)user_agent_hdr);
    strcat(headers, "Connection: close\r\n");
    strcat(headers, "Proxy-Connection: close\r\n");

    strcat(headers, "\r\n");

    if (cache_get(&cache, uri, &cached_data, &cached_size)) {
        rio_writen(clientfd, cached_data, cached_size);
        Free(cached_data);
        return;
    }

    serverfd = open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(clientfd, hostname, "502", "Bad Gateway",
                    "Proxy could not connect to the end server");
        return;
    }

    Rio_readinitb(&server_rio, serverfd);

    if (rio_writen(serverfd, headers, strlen(headers)) < 0) {
        Close(serverfd);
        return;
    }

    {
        char object_buf[MAX_OBJECT_SIZE];
        int object_size = 0;
        int cacheable = 1;

        while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
            if (rio_writen(clientfd, buf, n) < 0) {
                break;
            }

            if (cacheable) {
                if (object_size + n <= MAX_OBJECT_SIZE) {
                    memcpy(object_buf + object_size, buf, n);
                    object_size += n;
                } else {
                    cacheable = 0;
                }
            }
        }

        if (cacheable && object_size > 0) {
            cache_insert(&cache, uri, object_buf, object_size);
        }
    }

    Close(serverfd);
}

static cache_entry_t *cache_find(cache_t *cache, const char *uri)
{
    cache_entry_t *cur = cache->head;

    while (cur) {
        if (strcmp(cur->uri, uri) == 0)
            return cur;
        cur = cur->next;
    }
    return NULL;
}

static void cache_move_to_front(cache_t *cache, cache_entry_t *entry)
{
    if (cache->head == entry)
        return;

    if (entry->prev)
        entry->prev->next = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;
    if (cache->tail == entry)
        cache->tail = entry->prev;

    entry->prev = NULL;
    entry->next = cache->head;
    if (cache->head)
        cache->head->prev = entry;
    cache->head = entry;
    if (!cache->tail)
        cache->tail = entry;
}

static void cache_remove(cache_t *cache, cache_entry_t *entry)
{
    if (entry->prev)
        entry->prev->next = entry->next;
    if (entry->next)
        entry->next->prev = entry->prev;
    if (cache->head == entry)
        cache->head = entry->next;
    if (cache->tail == entry)
        cache->tail = entry->prev;

    cache->total_size -= entry->size;
    Free(entry->data);
    Free(entry);
}

static void cache_evict(cache_t *cache, int required)
{
    while (cache->tail && cache->total_size + required > MAX_CACHE_SIZE) {
        cache_remove(cache, cache->tail);
    }
}

void cache_init(cache_t *cache)
{
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    Sem_init(&cache->lock, 0, 1);
}

int cache_get(cache_t *cache, const char *uri, char **data, int *size)
{
    cache_entry_t *entry;

    P(&cache->lock);
    entry = cache_find(cache, uri);
    if (!entry) {
        V(&cache->lock);
        return 0;
    }

    *size = entry->size;
    *data = Malloc(entry->size);
    memcpy(*data, entry->data, entry->size);
    cache_move_to_front(cache, entry);
    V(&cache->lock);
    return 1;
}

void cache_insert(cache_t *cache, const char *uri, const char *data, int size)
{
    cache_entry_t *entry;

    if (size > MAX_OBJECT_SIZE)
        return;

    P(&cache->lock);
    cache_evict(cache, size);

    entry = Malloc(sizeof(cache_entry_t));
    strncpy(entry->uri, uri, MAXLINE - 1);
    entry->uri[MAXLINE - 1] = '\0';
    entry->data = Malloc(size);
    memcpy(entry->data, data, size);
    entry->size = size;
    entry->prev = NULL;
    entry->next = cache->head;

    if (cache->head)
        cache->head->prev = entry;
    cache->head = entry;
    if (!cache->tail)
        cache->tail = entry;

    cache->total_size += size;
    V(&cache->lock);
}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];
    char body[MAXBUF];
    int len = 0;

    len += sprintf(body + len, "<html><title>Proxy Error</title>");
    len += sprintf(body + len, "<body bgcolor=\"ffffff\">\r\n");
    len += sprintf(body + len, "%s: %s\r\n", errnum, shortmsg);
    len += sprintf(body + len, "<p>%s: %s\r\n", longmsg, cause);
    len += sprintf(body + len, "<hr><em>The Proxy Web server</em>\r\n");
    len += sprintf(body + len, "</body></html>");

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));

    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    Rio_writen(fd, body, strlen(body));
}