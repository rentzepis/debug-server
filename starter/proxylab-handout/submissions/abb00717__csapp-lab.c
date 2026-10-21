#include "csapp.h"
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

#define CACHE_OBJS 10

typedef struct {
    char uri[MAXLINE];
    char obj[MAX_OBJECT_SIZE];
    int size;
    int lru;
    int empty;
    int read_cnt;
    sem_t r_mutex;
    sem_t w_mutex;
} cache_block;

typedef struct {
    cache_block blocks[CACHE_OBJS];
    int lru_counter;
    sem_t lru_mutex;
} Cache;

Cache cache;

void init_cache();
int read_cache(char *uri, int fd);
void write_cache(char *uri, char *buf, int size);
void update_lru(int idx);

void *thread(void *vargp);
void doit(int fd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void build_http_header(char *http_header, char *hostname, char *path,
                       rio_t *client_rio);

int main(int argc, char **argv) {
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);
    init_cache();

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port,
                    MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char uri_copy[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char server_http_header[MAXLINE];
    int serverfd;
    rio_t client_rio, server_rio;

    Rio_readinitb(&client_rio, fd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;

    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement this method: %s\n", method);
        return;
    }

    strcpy(uri_copy, uri);

    // Check cache
    if (read_cache(uri, fd)) {
        return;
    }

    parse_uri(uri_copy, hostname, port, path);

    build_http_header(server_http_header, hostname, path, &client_rio);

    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        printf("connection failed\n");
        return;
    }

    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, server_http_header, strlen(server_http_header));

    char obj_buf[MAX_OBJECT_SIZE];
    int obj_size = 0;
    size_t n;
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) != 0) {
        Rio_writen(fd, buf, n);
        if (obj_size + n <= MAX_OBJECT_SIZE) {
            memcpy(obj_buf + obj_size, buf, n);
            obj_size += n;
        } else {
            obj_size = MAX_OBJECT_SIZE + 1; // Mark as too large to cache
        }
    }
    Close(serverfd);

    if (obj_size <= MAX_OBJECT_SIZE) {
        write_cache(uri, obj_buf, obj_size);
    }
}

void build_http_header(char *http_header, char *hostname, char *path,
                       rio_t *client_rio) {
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE],
        host_hdr[MAXLINE];

    /* Initialize buffers */
    request_hdr[0] = '\0';
    other_hdr[0] = '\0';
    host_hdr[0] = '\0';

    sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);

    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0) break;

        if (!strncasecmp(buf, "Host", strlen("Host"))) {
            strcpy(host_hdr, buf);
            continue;
        }

        if (!strncasecmp(buf, "Connection", strlen("Connection")) ||
            !strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) ||
            !strncasecmp(buf, "User-Agent", strlen("User-Agent"))) {
            continue;
        }

        if (strlen(other_hdr) + strlen(buf) < MAXLINE) {
            strcat(other_hdr, buf);
        }
    }

    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }

    sprintf(http_header, "%s%s%s%s%s%s\r\n", request_hdr, host_hdr,
            "Connection: close\r\n", "Proxy-Connection: close\r\n",
            user_agent_hdr, other_hdr);
}

void parse_uri(char *uri, char *hostname, char *port, char *path) {
    char *ptr = strstr(uri, "//");

    ptr = ptr != NULL ? ptr + 2 : uri;

    char *port_ptr = strstr(ptr, ":");
    char *path_ptr = strstr(ptr, "/");

    if (path_ptr != NULL) {
        strcpy(path, path_ptr);
        *path_ptr = '\0';
    } else {
        strcpy(path, "/");
    }

    if (port_ptr != NULL) {
        *port_ptr = '\0';
        strcpy(port, port_ptr + 1);
    } else {
        strcpy(port, "80");
    }

    strcpy(hostname, ptr);
}

void init_cache() {
    cache.lru_counter = 0;
    Sem_init(&cache.lru_mutex, 0, 1);
    for (int i = 0; i < CACHE_OBJS; i++) {
        cache.blocks[i].lru = 0;
        cache.blocks[i].empty = 1;
        cache.blocks[i].read_cnt = 0;
        Sem_init(&cache.blocks[i].r_mutex, 0, 1);
        Sem_init(&cache.blocks[i].w_mutex, 0, 1);
    }
}

void update_lru(int idx) {
    P(&cache.lru_mutex);
    cache.blocks[idx].lru = cache.lru_counter++;
    V(&cache.lru_mutex);
}

int read_cache(char *uri, int fd) {
    for (int i = 0; i < CACHE_OBJS; i++) {
        P(&cache.blocks[i].r_mutex);
        cache.blocks[i].read_cnt++;
        if (cache.blocks[i].read_cnt == 1) {
            P(&cache.blocks[i].w_mutex);
        }
        V(&cache.blocks[i].r_mutex);

        int hit = 0;
        if (!cache.blocks[i].empty && strcmp(uri, cache.blocks[i].uri) == 0) {
            Rio_writen(fd, cache.blocks[i].obj, cache.blocks[i].size);
            hit = 1;
        }

        P(&cache.blocks[i].r_mutex);
        cache.blocks[i].read_cnt--;
        if (cache.blocks[i].read_cnt == 0) {
            V(&cache.blocks[i].w_mutex);
        }
        V(&cache.blocks[i].r_mutex);

        if (hit) {
            update_lru(i);
            return 1;
        }
    }
    return 0;
}

void write_cache(char *uri, char *buf, int size) {
    int target_idx = -1;
    int min_lru = 0x7FFFFFFF;

    for (int i = 0; i < CACHE_OBJS; i++) {
        P(&cache.blocks[i].r_mutex);
        if (cache.blocks[i].empty) {
            target_idx = i;
            V(&cache.blocks[i].r_mutex);
            break;
        }
        if (cache.blocks[i].lru < min_lru) {
            target_idx = i;
            min_lru = cache.blocks[i].lru;
        }
        V(&cache.blocks[i].r_mutex);
    }

    P(&cache.blocks[target_idx].w_mutex);
    strcpy(cache.blocks[target_idx].uri, uri);
    memcpy(cache.blocks[target_idx].obj, buf, size);
    cache.blocks[target_idx].size = size;
    cache.blocks[target_idx].empty = 0;
    V(&cache.blocks[target_idx].w_mutex);

    update_lru(target_idx);
}
