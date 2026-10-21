/*
 * name: 郑斗薰
 * student-id: 2100094820
 */
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "csapp.h"


/* 连接头部 */
static const char *CONNECTION_HEADER = "Connection: close\r\n";
static const char *PROXY_CONNECTION_HEADER = "Proxy-Connection: close\r\n";

typedef struct cache_node {
    char uri[MAXLINE];
    char *data;
    int data_size;
    struct cache_node *prev;
    struct cache_node *next;
} cache_node_t;

typedef struct {
    cache_node_t *head;
    cache_node_t *tail;
    int current_size;
    pthread_rwlock_t lock;
} cache_t;

static cache_t cache;

void initialize_cache();
int check_cache(const char *uri, char **data, int *data_size);
void add_to_cache(const char *uri, const char *data, int size);
void move_node_to_front(cache_node_t *node);
void evict_cache(int size_needed);
void *handle_client_connection(void *arg);
void process_request(int client_fd);
void parse_uri(const char *uri, char *host, char *path, int *port);
void construct_http_request_header(char *header, const char *path, const char *host, int port, rio_t *client_rio);

// 缓存操作函数
void lock_cache(pthread_rwlock_t *lock) {
    pthread_rwlock_wrlock(lock);
}

void unlock_cache(pthread_rwlock_t *lock) {
    pthread_rwlock_unlock(lock);
}

// 初始化缓存系统
void initialize_cache() {
    cache.head = NULL;
    cache.tail = NULL;
    cache.current_size = 0;
    pthread_rwlock_init(&cache.lock, NULL);
}

// 查找缓存
int check_cache(const char *uri, char **cached_data, int *cached_data_size) {
    lock_cache(&cache.lock);
    cache_node_t *node = cache.head;
    while (node) {
        if (!strcmp(node->uri, uri)) {
            int node_data_size = node->data_size;
            char *data_copy = (char *)Malloc(node_data_size);
            memcpy(data_copy, node->data, node_data_size);
            move_node_to_front(node);
            *cached_data = data_copy;
            *cached_data_size = node_data_size;
            unlock_cache(&cache.lock);
            return 1;
        }
        node = node->next;
    }
    unlock_cache(&cache.lock);
    return 0;
}

// 添加缓存
void add_to_cache(const char *uri, const char *data, int size) {
    if (size > MAX_OBJECT_SIZE) {
        return;
    }

    lock_cache(&cache.lock);
    evict_cache(size);

    cache_node_t *new_node = (cache_node_t *)Malloc(sizeof(cache_node_t));
    strcpy(new_node->uri, uri);
    new_node->data = (char *)Malloc(size);
    memcpy(new_node->data, data, size);
    new_node->data_size = size;
    new_node->prev = NULL;
    new_node->next = NULL;

    if (!cache.head) {
        cache.head = new_node;
        cache.tail = new_node;
    } else {
        new_node->next = cache.head;
        cache.head->prev = new_node;
        cache.head = new_node;
    }
    cache.current_size += size;
    unlock_cache(&cache.lock);
}

// 将缓存节点移动到队头
void move_node_to_front(cache_node_t *node) {
    if (cache.head == node) return;
    if (node->prev) node->prev->next = node->next;
    if (node->next) node->next->prev = node->prev;
    if (cache.tail == node) cache.tail = node->prev;
    node->prev = NULL;
    node->next = cache.head;
    if (cache.head) cache.head->prev = node;
    cache.head = node;
}

// 缓存替换策略：如果需要，清除旧缓存
void evict_cache(int size_needed) {
    while (cache.current_size + size_needed > MAX_CACHE_SIZE) {
        if (!cache.tail) break;
        cache_node_t *victim_node = cache.tail;
        cache.tail = victim_node->prev;
        if (cache.tail) {
            cache.tail->next = NULL;
        } else {
            cache.head = NULL;
        }
        cache.current_size -= victim_node->data_size;
        free(victim_node->data);
        free(victim_node);
    }
}

// 客户端连接处理线程
void *handle_client_connection(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);
    Pthread_detach(pthread_self());
    process_request(client_fd);
    Close(client_fd);
    return NULL;
}

// 处理客户端请求
void process_request(int client_fd) {
    char buffer[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t client_rio;
    Rio_readinitb(&client_rio, client_fd);

    if (!Rio_readlineb(&client_rio, buffer, MAXLINE)) return;
    sscanf(buffer, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        fprintf(stderr, "Proxy does not support method: %s\n", method);
        return;
    }

    char *cached_data = NULL;
    int cached_data_size = 0;

    // 如果缓存中存在数据，直接返回
    if (check_cache(uri, &cached_data, &cached_data_size)) {
        Rio_writen(client_fd, cached_data, cached_data_size);
        free(cached_data);
        return;
    }

    char host[MAXLINE], path[MAXLINE];
    int port = 80;
    parse_uri(uri, host, path, &port);

    char request_header[MAXLINE * 10];
    construct_http_request_header(request_header, path, host, port, &client_rio);

    char port_str[16];
    sprintf(port_str, "%d", port);
    int server_fd = Open_clientfd(host, port_str);
    if (server_fd < 0) {
        fprintf(stderr, "Unable to connect to server (%s:%d)\n", host, port);
        return;
    }

    Rio_writen(server_fd, request_header, strlen(request_header));

    rio_t server_rio;
    Rio_readinitb(&server_rio, server_fd);

    char *response_data = (char *)Malloc(MAX_OBJECT_SIZE);
    int total_data_size = 0;
    size_t data_chunk_size;

    while ((data_chunk_size = Rio_readnb(&server_rio, buffer, MAXLINE)) > 0) {
        Rio_writen(client_fd, buffer, data_chunk_size);
        if (total_data_size + (int)data_chunk_size <= MAX_OBJECT_SIZE) {
            memcpy(response_data + total_data_size, buffer, data_chunk_size);
        }
        total_data_size += data_chunk_size;
    }
    Close(server_fd);

    if (total_data_size <= MAX_OBJECT_SIZE) {
        add_to_cache(uri, response_data, total_data_size);
    }

    free(response_data);
}

// 解析URI
void parse_uri(const char *uri, char *host, char *path, int *port) {
    const char *protocol_pos = strstr(uri, "://");
    protocol_pos = protocol_pos ? protocol_pos + 3 : uri;

    const char *path_start = strchr(protocol_pos, '/');
    strcpy(path, path_start ? path_start : "/");

    char host_buffer[MAXLINE];
    strcpy(host_buffer, protocol_pos);
    char *slash_pos = strchr(host_buffer, '/');
    if (slash_pos) *slash_pos = '\0';

    char *colon_pos = strchr(host_buffer, ':');
    if (colon_pos) {
        *colon_pos = '\0';
        *port = atoi(colon_pos + 1);
    }

    strcpy(host, host_buffer);
}

// 构建HTTP请求头
void construct_http_request_header(char *header, const char *path, const char *host, int port, rio_t *client_rio) {
    char temp[MAXLINE], request_header[MAXLINE] = "";
    sprintf(request_header, "GET %s HTTP/1.0\r\n", path);

    char host_header[MAXLINE];
    sprintf(host_header, "Host: %s:%d", host, port);
    strcat(request_header, host_header);
    strcat(request_header, user_agent_hdr);
    strcat(request_header, CONNECTION_HEADER);
    strcat(request_header, PROXY_CONNECTION_HEADER);

    while (1) {
        if (!Rio_readlineb(client_rio, temp, MAXLINE)) break;
        if (!strcmp(temp, "\r\n")) break;

        char temp_lower[MAXLINE];
        strcpy(temp_lower, temp);
        for (char *p = temp_lower; *p; p++) *p = tolower(*p);

        if (strstr(temp_lower, "host:") == temp_lower ||
            strstr(temp_lower, "connection:") == temp_lower ||
            strstr(temp_lower, "proxy-connection:") == temp_lower ||
            strstr(temp_lower, "user-agent:") == temp_lower) {
            continue;
        }
        strcat(request_header, temp);
    }
    
    strcat(request_header, "\r\n");
    strcpy(header, request_header);
}

// 主函数
int main(int argc, char **argv) {
    Signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    initialize_cache();

    int listen_fd = Open_listenfd(argv[1]);
    if (listen_fd < 0) {
        fprintf(stderr, "Failed to open listenfd on port %s\n", argv[1]);
        exit(1);
    }

    while (1) {
        struct sockaddr_storage client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = Accept(listen_fd, (SA *)&client_addr, &client_len);

        int *client_fd_ptr = (int *)Malloc(sizeof(int));
        *client_fd_ptr = client_fd;

        pthread_t thread_id;
        Pthread_create(&thread_id, NULL, handle_client_connection, client_fd_ptr);
    }

    Close(listen_fd);
    return 0;
}
