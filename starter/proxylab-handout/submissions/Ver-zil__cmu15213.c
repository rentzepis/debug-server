#include "csapp.h"
#include <stdio.h>

// ======================== cache定义 ====================================
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_node {
    struct cache_node *prev;
    struct cache_node *next;
    char *object;
    ssize_t size;
    char url[MAXLINE];
} cache_node;

typedef struct {
    cache_node *head;
    cache_node *tail;
    int total_size;
    sem_t mutex;
} cache;

cache_node *cache_node_create(char *url) {
    cache_node *node = Malloc(sizeof(cache_node));
    node->prev = NULL;
    node->next = NULL;
    node->object = Malloc(MAX_OBJECT_SIZE);
    node->size = 0;
    strcpy(node->url, url);
    return node;
}

void cache_node_delete(cache_node *node) {
    Free(node->object);
    Free(node);
}

void cache_init(cache **c) {
    *c = Malloc(sizeof(cache));
    (*c)->head = Malloc(sizeof(cache_node));
    (*c)->tail = Malloc(sizeof(cache_node));
    (*c)->head->url[0] = '\0';
    (*c)->tail->url[0] = '\0';
    (*c)->head->prev = NULL;
    (*c)->head->next = (*c)->tail;
    (*c)->tail->prev = (*c)->head;
    (*c)->tail->next = NULL;
    (*c)->total_size = 0;
    Sem_init(&(*c)->mutex, 0, 1);
}

void cache_deinit(cache **c) {
    cache_node *cur = (*c)->head;
    while (cur != NULL) {
        cache_node *next = cur->next;
        cache_node_delete(cur);
        cur = next;
    }
    Free(*c);
}

cache *cache_pool;
// =======================================================================

// ======================== MIT 6.5840 风格调试打印 ========================
#define DEBUG 1
#if DEBUG
#define DPrintf(format, ...)                                                                       \
    printf("[DEBUG] | %s():%d | " format "\n", __func__, __LINE__, ##__VA_ARGS__)
#else
#define DPrintf(format, ...)
#endif
// =======================================================================

// ======================== 预线程定义 ========================
#define NTHREADS 4
#define SBUFSIZE 16

typedef struct {
    int *buf;    // arrat
    int n;       // buf最大容量
    int front;   // buf[(front+1)%n] 就是第一个元素
    int rear;    // buf[rear%n]就是最后一个元素
    sem_t mutex; // 共用
    sem_t slots; // 生产者用
    sem_t items; // 消费者用
} sbuf_t;

void sbuf_init(sbuf_t *sp, int n) {
    sp->buf = Malloc(n * sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp) {
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item) {
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear) % sp->n] = item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t *sp) {
    P(&sp->items);
    P(&sp->mutex);
    int item = sp->buf[(++sp->front) % sp->n];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

sbuf_t sbuf;
// =======================================================================

void doit(int clientfd);
void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_url(char *url, char *host, char *port, char *uri);
void build__request(rio_t *client_rio, int serverfd, char *uri, char *host);
void read_client_requesthdrs(rio_t *client_rio);
void build_response(int serverfd, int clientfd, char *url);

void cache_pop_node(cache_node *node);
void cache_insert_node(cache_node *node);
void cache_update_node(cache_node *node);
int cache_hit(char *url, int clientfd);

void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "main: %s \n", argv[0]);
        exit(1);
    }

    struct sockaddr_storage client_addr;
    socklen_t client_len;
    pthread_t tid;

    cache_init(&cache_pool);
    sbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < NTHREADS; i++) {
        if (pthread_create(&tid, NULL, thread, NULL) != 0) {
            fprintf(stderr, "main: pthread_create error\n");
            exit(1);
        }
    }

    // 暴露client连接端口
    int listenfd = Open_listenfd(argv[1]);
    int connectfd;
    while (1) {
        client_len = sizeof(client_addr);
        connectfd = Accept(listenfd, (SA *)&client_addr, &client_len);
        sbuf_insert(&sbuf, connectfd);
    }

    printf("%s", user_agent_hdr);
    return 0;
}

void cache_pop_node(cache_node *node) {
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = NULL;
    node->next = NULL;
    cache_pool->total_size -= node->size;
}

/**
 * 缓存不够的时候一直删除尾部的node，直到有足够的空间插入新的node
 * 插入新的node到头部
 */
void cache_insert_node(cache_node *node) {
    while (cache_pool->total_size + node->size > MAX_CACHE_SIZE) {
        cache_node *last = cache_pool->tail->prev;
        cache_pop_node(last);
        cache_node_delete(last);
    }

    node->prev = cache_pool->head;
    node->next = cache_pool->head->next;
    node->prev->next = node;
    node->next->prev = node;
    cache_pool->total_size += node->size;
}

void cache_update_node(cache_node *node) {
    cache_pop_node(node);
    cache_insert_node(node);
}

int cache_hit(char *url, int clientfd) {
    int hit = 0;
    P(&cache_pool->mutex);

    cache_node *cur = cache_pool->head->next;
    while (cur != cache_pool->tail) {
        if (strcmp(cur->url, url) == 0) {
            hit = 1;
            Rio_writen(clientfd, cur->object, cur->size);
            cache_update_node(cur);
            break;
        }
        cur = cur->next;
    }

    V(&cache_pool->mutex);
    return hit;
}

void *thread(void *vargp) {
    // 线程分离，线程结束后自动回收资源，不需要主线程调用pthread_join来回收资源
    pthread_detach(pthread_self());
    while (1) {
        int clientfd = sbuf_remove(&sbuf);
        doit(clientfd);
        Close(clientfd);
    }
    return NULL;
}

// 读取client的连接请求，连接到server端
void doit(int clientfd) {
    char buf[MAXLINE];
    char method[MAXLINE], url[MAXLINE], ver[MAXLINE]; // proxy里，uri变成了url
    rio_t client_rio;
    Rio_readinitb(&client_rio, clientfd);
    Rio_readlineb(&client_rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, url, ver);

    DPrintf("method:%s url:%s ver:%s \n", method, url, ver);

    if (strcasecmp(method, "GET")) {
        client_error(clientfd, method, "501", "Not Implemented",
                     "Proxy doesn't implement this method");
        return;
    }

    if (cache_hit(url, clientfd)) {
        DPrintf("cache hit \n");
        return;
    }

    // 开始建立目标server的连接
    char host[MAXLINE], port[MAXLINE], uri[MAXLINE], url_copy[MAXLINE];
    strcpy(url_copy, url);
    if (!parse_url(url_copy, host, port, uri)) {
        client_error(clientfd, method, "400", "Bad Request",
                     "Invalid URL format or non-HTTP protocol");
        return;
    }

    int serverfd = Open_clientfd(host, port);
    if (serverfd < 0) {
        client_error(clientfd, method, "502", "Bad Gateway",
                     "Proxy couldn't connect to the server");
        return;
    }

    // 将request line修改后发送给server端，再将其他部分全部发送给服务端
    build__request(&client_rio, serverfd, uri, host);
    read_client_requesthdrs(&client_rio);

    // 读取server端的响应，并返回给client端
    build_response(serverfd, clientfd, url);
    Close(serverfd);
    DPrintf("close \n");
}

// 从server端读取响应，并发送给client端
void build_response(int serverfd, int clientfd, char *url) {
    rio_t server_rio;
    char buf[MAXLINE];
    ssize_t n;
    ssize_t total_size = 0;
    cache_node *node = cache_node_create(url);
    Rio_readinitb(&server_rio, serverfd);
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        Rio_writen(clientfd, buf, n); // 用实际读取的字节数n，不是strlen
        total_size += n;

        if (total_size > MAX_OBJECT_SIZE) {
            continue;
        }

        memcpy(node->object + total_size - n, buf, n);
    }

    if (total_size > MAX_OBJECT_SIZE)
        cache_node_delete(node);
    else {
        node->size = total_size;
        DPrintf("cache insert url:%s size:%zd \n", url, node->size);
        P(&cache_pool->mutex);
        cache_insert_node(node);
        V(&cache_pool->mutex);
    }
}

// 将client的剩余请求头读完
void read_client_requesthdrs(rio_t *client_rio) {
    char buf[MAXLINE];
    Rio_readlineb(client_rio, buf, MAXLINE);
    // 假如读的时候出问题了，是不是一直停不下来了？
    while (strcmp(buf, "\r\n")) {
        Rio_readlineb(client_rio, buf, MAXLINE);
        DPrintf("read hdrs:%s", buf);
    }
}

// 给server端构建request line和hdrs，并发送给server端
void build__request(rio_t *client_rio, int serverfd, char *uri, char *host) {
    char buf[MAXLINE];
    sprintf(buf, "GET %s HTTP/1.0\r\n", uri);
    Rio_writen(serverfd, buf, strlen(buf));
    DPrintf("send line:%s", buf);

    sprintf(buf, "Host: %s\r\n", host);
    Rio_writen(serverfd, buf, strlen(buf));
    DPrintf("send hdrs:%s", buf);

    sprintf(buf, "%s", user_agent_hdr);
    Rio_writen(serverfd, buf, strlen(buf));
    DPrintf("send hdrs:%s", buf);

    sprintf(buf, "Connection: close\r\n");
    Rio_writen(serverfd, buf, strlen(buf));
    DPrintf("send hdrs:%s", buf);

    sprintf(buf, "Proxy-Connection: close\r\n");
    Rio_writen(serverfd, buf, strlen(buf));
    DPrintf("send hdrs:%s", buf);

    sprintf(buf, "\r\n");
    Rio_writen(serverfd, buf, strlen(buf));
}

// 将url拆分成需要的参数
int parse_url(char *url, char *host, char *port, char *uri) {
    if (!strstr(url, "http://"))
        return 0;

    char *p = url + 7;
    char *port_start = strchr(p, ':');
    char *uri_start = strchr(p, '/');

    if (port_start != NULL)
        *port_start = '\0';

    if (uri_start != NULL)
        *uri_start = '\0';

    strcpy(host, p);

    if (port_start != NULL)
        strcpy(port, port_start + 1);
    else
        strcpy(port, "80");

    strcpy(uri, "/");
    if (uri_start != NULL)
        strcpy(uri + 1, uri_start + 1);

    DPrintf("host:%s port:%s uri:%s \n", host, port, uri);

    return 1;
}

void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Proxy Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The CSAPP Proxy</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
