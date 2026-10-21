/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 * @author Yuchen Wu <yuchenwu@andrew.cmu.edu>
 */

/* Some useful includes to help you get started */

#include "csapp.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <http_parser.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* Struct for a cache block */
typedef struct cache_block cache_block;
struct cache_block {
    char *uri;             // uri stored by cache
    char *file;            // file stored by cache
    int size;              // size used by cache
    pthread_mutex_t mutex; // mutex
    cache_block *next;     // pointer for next block
    cache_block *front;    // pointer for next block
};
/* Define structure that implements cache */
typedef struct Cache {
    cache_block *cache_head; // head pointer
    int total_size;          // total size
    pthread_mutex_t mutex;   // mutex
} cache;
static cache Cache;
/* Function prototypes */

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/* Function prototypes */
void *thread(void *vargp);
void doit(int fd);
void clienterror(int fd, const char *cause, const char *errnum,
                 const char *shortmsg, const char *longmsg);

bool read_cache(const char *uri, int clientfd);
bool write_cache(const char *uri, const char *web_obj, int size);
cache_block *find(const char *uri);
void store(cache_block *cache);
/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "User-Agent: Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1\r\n";
/* String to use for the Connection header. */

int main(int argc, char **argv) {
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr clientaddr;
    pthread_t tid;
    // Ignore the SIGPIPE signal
    Signal(SIGPIPE, SIG_IGN);
    /* Check command-line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    // Initialize cache
    Cache.cache_head = NULL;
    pthread_mutex_init(&Cache.mutex, NULL);
    listenfd = open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        getnameinfo((struct sockaddr *)&clientaddr, clientlen, hostname,
                    MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_create(&tid, NULL, thread, connfd);
    }
    close(listenfd);
}
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    close(connfd);
    return NULL;
}
void doit(int clientfd) {
    char buf[MAXLINE];
    char *method, *host, *scheme, *uri, *port, *path, *version;
    header_t *header;
    char new_header[MAXLINE], header_host[MAXLINE], header_temp[MAXLINE],
        header_other[MAXLINE];
    rio_t rio_client, rio_server;
    parser_t *parser;
    /* Read and parse the client's request line */
    rio_readinitb(&rio_client, clientfd);
    rio_readlineb(&rio_client, buf, MAXLINE);
    parser = parser_new();
    if (parser_parse_line(parser, buf) != REQUEST) {
        fprintf(stderr, "parser_parse_line error\n");
        parser_free(parser);
        return;
    }
    parser_retrieve(parser, METHOD, (const char **)&method);
    if (strcmp(method, "GET")) {
        clienterror(clientfd, method, "501", "Not implemented",
                    "Proxy does not implement this method");
        parser_free(parser);
        return;
    }
    parser_retrieve(parser, HOST, (const char **)&host);
    parser_retrieve(parser, SCHEME, (const char **)&scheme);
    parser_retrieve(parser, URI, (const char **)&uri);
    parser_retrieve(parser, PORT, (const char **)&port);
    parser_retrieve(parser, PATH, (const char **)&path);
    parser_retrieve(parser, HTTP_VERSION, (const char **)&version);
    rio_readlineb(&rio_client, buf, MAXLINE);
    while (strcmp(buf, "\r\n")) {
        rio_readlineb(&rio_client, buf, MAXLINE);
        parser_parse_line(parser, buf);
        printf("%s", buf);
    }

    // Check if the URI is cached
    if (read_cache(uri, clientfd) == 0) {
        parser_free(parser);
        return;
    }

    memset(header_host, '\0', sizeof(header_host));
    memset(header_other, '\0', sizeof(header_other));
    sprintf(header_temp, "GET %s HTTP/1.0\r\n", path);
    strcat(header_other, header_temp);
    while (1) {
        if ((header = parser_retrieve_next_header(parser)) == NULL) {
            break;
        }
        if (strcmp(header->name, "Host") == 0) {
            sprintf(header_host, "%s: %s\r\n", header->name, header->value);
        } else if (strcmp(header->name, "User-Agent") == 0) {
            strcat(header_other, header_user_agent);
        } else {
            sprintf(header_temp, "%s%s: %s\r\n", header_temp, header->name,
                    header->value);
            strcat(header_other, header_temp);
        }
    }
    if (strlen(header_host) == 0) {
        sprintf(header_host, "Host: %s:%s\r\n", host, port);
    }
    sprintf(new_header, "%s%s\r\n", header_other, header_host);
    int proxyfd = open_clientfd(host, port);
    rio_readinitb(&rio_server, proxyfd);
    if (rio_writen(proxyfd, new_header, strlen(new_header)) <= 0) {
        perror("rio_writen error");
        parser_free(parser);
        return;
    }

    // Array to store the web object temporarily in case it needs to be cached
    char temp[MAX_OBJECT_SIZE];

    // Read the response from the server
    // Use rio_readnb to avoid reading line by line
    int n = 0, size = 0;
    while ((n = rio_readnb(&rio_server, buf, MAXLINE)) != 0) {
        printf(stderr, "server received %d bytes\n", (int)n);
        rio_writen(clientfd, buf, n);
        if (size + n <= MAX_OBJECT_SIZE) {
            memcpy(temp + size, buf, n);
        }
        size += n;
    }

    // Write to cache if and only if size <= MAX_OBJECT_SIZE
    if (size <= MAX_OBJECT_SIZE) {
        write_cache(uri, temp, size);
    }
    parser_free(parser);
    return;
}

void clienterror(int fd, const char *cause, const char *errnum,
                 const char *shortmsg, const char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];
    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

/**
 * Read the uri inside the cache
 */
bool read_cache(const char *uri, int clientfd) {
    pthread_mutex_lock(&Cache.mutex);
    // Cache found, write web object to client
    // Look for the cache block corresponding to the URI
    cache_block *cache = find(uri);
    if (cache == NULL) {
        pthread_mutex_unlock(&Cache.mutex);
        return 1;
    }
    char temp[MAX_OBJECT_SIZE];
    memcpy(temp, cache->file, cache->size);
    // Update the cache
    if (Cache.cache_head == cache) {
        Cache.cache_head = Cache.cache_head->next;
        cache->next = NULL;
        store(cache);
    } else {
        cache_block *cur = Cache.cache_head;
        while (cur && cur->next != cache) {
            cur = cur->next;
        }
        cur->next = cache->next;
        cache->next = NULL;
        while (cur->next != NULL) {
            cur = cur->next;
        }
        cur->next = cache;
    }
    pthread_mutex_unlock(&Cache.mutex);
    // Write the web object to the client
    rio_writen(clientfd, temp, cache->size);
    return 0;
}

/**
 * store the uri in the cache
 */
bool write_cache(const char *uri, const char *web_obj, int size) {
    pthread_mutex_lock(&Cache.mutex);

    // Do not write if the web object is already in the cache
    if (find(uri)) {
        fprintf(stderr, "write_cache error: web object exists in cache\n");
        pthread_mutex_unlock(&Cache.mutex);
        return 1;
    }

    // Add the size of the new web object to the total cache size
    Cache.total_size += size;

    // Evict LRU cache if total cache size is too large
    if (Cache.total_size > MAX_CACHE_SIZE) {
        fprintf(stderr, "Total cache size exceeded, evicting LRU cache\n");
        // Continuously evict LRU cache until total size is under the limit
        cache_block *LRU_cache = Cache.cache_head;
        while (Cache.total_size > MAX_CACHE_SIZE) {
            // remove the uri by using the LRU method
            cache_block *cur = LRU_cache;
            int size = LRU_cache->size;
            pthread_mutex_lock(&cur->mutex);
            Free(cur->uri);
            Free(cur->file);
            pthread_mutex_unlock(&cur->mutex);
            pthread_mutex_destroy(&cur->mutex);
            Free(cur);
            LRU_cache = LRU_cache->next;
            Cache.total_size -= size;
        }
        Cache.cache_head = LRU_cache;
    }

    // Write new cache block
    cache_block *new = NULL;
    new = Malloc(sizeof(*new));
    pthread_mutex_init(&new->mutex, NULL);
    pthread_mutex_lock(&new->mutex);
    new->uri = Calloc(MAXLINE, sizeof(char));
    memcpy(new->uri, uri, strlen(uri));
    new->file = Calloc(MAX_OBJECT_SIZE, sizeof(char));
    memcpy(new->file, web_obj, size);
    new->size = size;
    new->next = NULL;
    new->front = NULL;
    pthread_mutex_unlock(&new->mutex);

    // Insert new cache block into the linked list
    store(new);

    pthread_mutex_unlock(&Cache.mutex);
    return 0;
}

/**
 * find the uri inside the cache
 */
cache_block *find(const char *uri) {
    cache_block *current_cache = Cache.cache_head;
    while (current_cache) {
        if (strcmp(current_cache->uri, uri) == 0) {
            break;
        }
        current_cache = current_cache->next;
    }
    return current_cache;
}
/**
 * Store the uri into the cache
 */
void store(cache_block *cache) {
    cache_block *current_cache = Cache.cache_head;
    while (current_cache != NULL) {
        cache->front = current_cache;
        current_cache = current_cache->next;
    }
    if (cache->front == NULL) {
        Cache.cache_head = cache;
        return;
    }
    cache->front->next = cache;
    return;
}
