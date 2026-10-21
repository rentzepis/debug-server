#include <stdio.h>
#include "csapp.h"
#include <pthread.h>
#include <time.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_BLOCKS 10

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static struct timespec program_start;

/* Function Prototypes */
void doit(int fd);
void parseuri(char *uri, char *hostname, char *port, char *path);
void read_request_headers(rio_t *rio, char *extra_hdrs);
void *thread_routine(void *vargp);
int cache_read(char *url, int fd);
void cache_write(char *url, char *buffer, int size);

typedef struct
{
    char cache_object[MAX_OBJECT_SIZE];
    char cache_key_url[MAXLINE];
    int content_size;
    unsigned int lru_counter;
    int is_valid;

} cache_block;

typedef struct
{
    cache_block blocks[MAX_CACHE_BLOCKS];
    unsigned int current_time;
    pthread_rwlock_t rwlock;
} proxy_cache;

static proxy_cache cache;

int main(int argc, char **argv)
{
    int listenfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* Lab Requirement: Ignore SIGPIPE so the proxy doesn't crash */
    Signal(SIGPIPE, SIG_IGN);

    /* Initialize program start time for logging */
    clock_gettime(CLOCK_MONOTONIC, &program_start);

    // initialize the cache
    pthread_rwlock_init(&(cache.rwlock), NULL);
    cache.current_time = 0;
    for (int i = 0; i < MAX_CACHE_BLOCKS; i++)
    {
        cache.blocks[i].is_valid = 0;
        cache.blocks[i].lru_counter = 0;
    }

    listenfd = Open_listenfd(argv[1]); // listen to client
    while (1)
    {
        clientlen = sizeof(clientaddr);
        int *connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        pthread_t tid;
        Pthread_create(&tid, NULL, thread_routine, connfdp);

        // for logging purpose
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
    }
    return 0;
}

void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char extra_hdrs[MAXLINE] = "";
    char final_request[MAXLINE];
    int serverfd;
    rio_t rio_client, rio_server;
    ssize_t n;

    // cache related variables
    int total_size = 0;
    int cache_eligible = 1;
    char cache_buffer[MAX_OBJECT_SIZE];
    char cache_key[MAXLINE];

    /* 1. Read Request Line */
    Rio_readinitb(&rio_client, fd);
    if (!Rio_readlineb(&rio_client, buf, MAXLINE))
        return;

    sscanf(buf, "%s %s %s", method, uri, version);
    strcpy(cache_key, uri);
    if (strcasecmp(method, "GET"))
    {
        printf("Proxy only handles GET requests.\n");
        return;
    }

    /* 2. Parse URI */
    parseuri(uri, hostname, port, path);

    // if url is in cache, we just find and return it
    if (cache_read(cache_key, fd))
        return;
    /* 3. Read and collect headers from the client */
    read_request_headers(&rio_client, extra_hdrs);

    /* 4. Build the final request string for the server */
    sprintf(final_request, "GET %s HTTP/1.0\r\n", path); // Always HTTP/1.0
    sprintf(final_request + strlen(final_request), "Host: %s\r\n", hostname);
    strcat(final_request, user_agent_hdr);
    strcat(final_request, "Connection: close\r\n");
    strcat(final_request, "Proxy-Connection: close\r\n");
    strcat(final_request, extra_hdrs); // Add any cookies/extra info from client
    strcat(final_request, "\r\n");     // Final blank line

    /* 5. Connect to Server and Send Request */
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0)
        return;

    Rio_writen(serverfd, final_request, strlen(final_request));

    /* 6. Forward Response (Binary Safe) */
    Rio_readinitb(&rio_server, serverfd);
    while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0)
    {
        Rio_writen(fd, buf, n);
        if (cache_eligible)
        {
            if (total_size + n <= MAX_OBJECT_SIZE)
            {
                // cache buffer + total size is starting point of the destination
                // copy n size of buf data starting at cache buffer+ total size
                memcpy(cache_buffer + total_size, buf, n);
                total_size += n;
            }
            else
                cache_eligible = 0;
        }
    }
    if (cache_eligible && total_size > 0)
    {
        cache_write(cache_key, cache_buffer, total_size);
    }

    Close(serverfd);
}

void read_request_headers(rio_t *rio, char *extra_hdrs)
{
    char buf[MAXLINE];

    while (Rio_readlineb(rio, buf, MAXLINE) > 0)
    {
        /* Stop at the blank line */
        if (!strcmp(buf, "\r\n") || !strcmp(buf, "\n"))
            break;

        /* Forward only headers that we aren't already hardcoding in doit */
        if (strstr(buf, "Host:") || strstr(buf, "User-Agent:") ||
            strstr(buf, "Connection:") || strstr(buf, "Proxy-Connection:"))
        {
            continue;
        }
        strcat(extra_hdrs, buf);
    }
}

void parseuri(char *uri, char *hostname, char *port, char *path)
{
    char *hostptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
    char *pathptr = strstr(hostptr, "/");

    if (pathptr)
    {
        strcpy(path, pathptr);
        *pathptr = '\0';
    }
    else
    {
        strcpy(path, "/");
    }

    char *portptr = strstr(hostptr, ":");
    if (portptr)
    {
        strcpy(port, portptr + 1);
        *portptr = '\0';
    }
    else
    {
        strcpy(port, "80");
    }
    strcpy(hostname, hostptr);
}

void *thread_routine(void *vargp)
{
    int connfd = *((int *)vargp);
    Free(vargp);
    Pthread_detach(Pthread_self());

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double elapsed_from_start = (now.tv_sec - program_start.tv_sec) + (now.tv_nsec - program_start.tv_nsec) / 1e9;
    printf("[%.3f] Thread %ld: START\n", elapsed_from_start, pthread_self());

    struct timespec req_start = now;
    doit(connfd);
    Close(connfd);

    clock_gettime(CLOCK_MONOTONIC, &now);
    elapsed_from_start = (now.tv_sec - program_start.tv_sec) + (now.tv_nsec - program_start.tv_nsec) / 1e9;
    double request_time = (now.tv_sec - req_start.tv_sec) + (now.tv_nsec - req_start.tv_nsec) / 1e9;
    printf("[%.3f] Thread %ld: END (%.3f sec)\n", elapsed_from_start, pthread_self(), request_time);

    return NULL;
}

int cache_read(char *url, int fd)
{
    // read lock so multiple thread can read it same time
    pthread_rwlock_rdlock(&(cache.rwlock));
    for (int i = 0; i < MAX_CACHE_BLOCKS; i++)
    {
        // this is a hit
        if (cache.blocks[i].is_valid && !strcmp(url, cache.blocks[i].cache_key_url))
        {
            // cache read here writes the data back to the client
            Rio_writen(fd, cache.blocks[i].cache_object, cache.blocks[i].content_size);
            cache.blocks[i].lru_counter = __sync_add_and_fetch(&cache.current_time, 1);
            pthread_rwlock_unlock(&(cache.rwlock));
            return 1;
        }
    }
    pthread_rwlock_unlock(&(cache.rwlock));
    return 0;
}

void cache_write(char *url, char *buffer, int size)
{
    pthread_rwlock_wrlock(&cache.rwlock);
    int target = 0;
    unsigned int min_lru = 0xFFFFFFFF;
    for (int i = 0; i < MAX_CACHE_BLOCKS; i++)
    {
        if (!cache.blocks[i].is_valid)
        {
            // find empty slot
            target = i;
            break;
        }
        if (cache.blocks[i].lru_counter < min_lru)
        {
            min_lru = cache.blocks[i].lru_counter;
            target = i;
        }
    }
    cache.blocks[target].is_valid = 1;
    cache.blocks[target].content_size = size;
    cache.current_time++;
    cache.blocks[target].lru_counter = cache.current_time;
    strcpy(cache.blocks[target].cache_key_url, url);
    memcpy(cache.blocks[target].cache_object, buffer, size);
    pthread_rwlock_unlock(&cache.rwlock);
}