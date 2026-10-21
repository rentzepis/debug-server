#include <stdio.h>
#include <stdatomic.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_LINE 1024

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


void doit(int);
int parse_http_url(char *url, char *host, char *port, char *uri);

_Atomic unsigned int global_time = 0;

typedef struct {
    char cache_uri[MAX_LINE];               // 缓存的 URI，例如 /home.html"
    char cache_obj[MAX_OBJECT_SIZE];        // 缓存的 Body 内容，注意是二进制，用 char 数组存
    int cache_len;                          // 当前内容的实际字节长度
    unsigned int lr_counter;                // LRU 计数器，数值越小说明越久没被访问
    int is_valid;                           // 显式标记：0 代表格子是空的，1 代表里面有货
} cache_block;

#define CACHE_OBJS_COUNT 10
cache_block cache_vec[CACHE_OBJS_COUNT];
pthread_rwlock_t cache_rwlock;

int main(int argc, char **argv)
{
    int listenfd, clientfd;
    unsigned int clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAX_LINE], client_port[MAX_LINE];
    pthread_t tid;
    void *thread(void *vargp);

    //初始化cache和锁
    pthread_rwlock_init(&cache_rwlock, NULL);
    for (int i = 0; i < CACHE_OBJS_COUNT; i++) {
        cache_vec[i].cache_len = 0;
        cache_vec[i].is_valid = 0;
        cache_vec[i].lr_counter = 0;
    }

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
    }
    fprintf (stdout, "listening: %s\n", argv[1]);
    fflush (stdout);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        clientfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAX_LINE, client_port, MAX_LINE, 0);
        printf("client connection: %s:%s\n", client_hostname, client_port);
        // doit(clientfd, client_hostname, client_port);
        Pthread_create(&tid, NULL, thread, (void *)clientfd);
    }

    printf("%s", user_agent_hdr);
    return 0;
}

void *thread(void *vargp) {
    int clientfd = (int)vargp;
    Pthread_detach(pthread_self());
    doit(clientfd);
    Close(clientfd);
    return NULL;
}

void doit(int clientfd) {
    char buf[MAX_LINE], method[MAX_LINE], uri[MAX_LINE], version[MAX_LINE];
    ssize_t n;
    rio_t rio_client, rio_server;
    Rio_readinitb(&rio_client, clientfd);
    // 获取request line: method url version, 确定目的地址(http在通过代理连接时，url会是绝对地址)
    n = Rio_readlineb(&rio_client, buf, MAX_LINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        printf("error: %s is not a GET request\n", method);
        return;
    }
    // 解析server_host和port
    char server_hostname[MAX_LINE], server_port[MAX_LINE], server_uri[MAX_LINE];
    parse_http_url(uri, server_hostname, server_port, server_uri);
    printf("get server info: %s %s %s\n", server_hostname, server_port, server_uri);
    fflush(stdout);

    // 第一步去cache中找uri，找到了就直接返回了.cache_vec是共享的，注意使用读写锁访问
    pthread_rwlock_rdlock(&cache_rwlock);
    for (int i = 0; i < CACHE_OBJS_COUNT; i++) {
        if (cache_vec[i].is_valid) {
            if (strcmp(cache_vec[i].cache_uri, server_uri) == 0) { // hit
                Rio_writen(clientfd, cache_vec[i].cache_obj, cache_vec[i].cache_len);
                cache_vec[i].lr_counter = atomic_fetch_add(&global_time, 1) + 1;
                return;
            }
        }else {
            continue;
        }
    }
    pthread_rwlock_unlock(&cache_rwlock);

    // cache miss
    // 建立与url的连接，将完整报文转发给url
    int serverfd = Open_clientfd(server_hostname, server_port);
    Rio_readinitb(&rio_server, serverfd);

    // 转发请求头，注意绝对地址改相对uri
    sprintf(buf, "GET %s %s\r\n", server_uri, version);
    printf("tst buf: %s",buf);
    Rio_writen(serverfd, buf, n);

    // 注意必须先转发完整个报文，才能读server的回复，没发完就读会阻塞超时
    while (n = Rio_readlineb(&rio_client, buf, MAX_LINE)) {
        fprintf(stdout,"client: %s", buf);
        fflush(stdout);
        Rio_writen(serverfd, buf, n);
        if (strcmp(buf,"\r\n") == 0) { // 发完了header
            break;
        }
    }

    // 读取server的回复，并转发给client
    // 将server的回复缓存到cache中，并使用LRU更新cache
    cache_block cache_tmp;
    snprintf(cache_tmp.cache_uri, sizeof(cache_tmp.cache_uri), "%s", server_uri);
    cache_tmp.cache_len = 0;
    cache_tmp.lr_counter = atomic_fetch_add(&global_time, 1) + 1;
    cache_tmp.is_valid = 1;
    while (n = Rio_readlineb(&rio_server, buf, MAX_LINE)) {
        fprintf(stdout,"server: %s", buf);
        fflush(stdout);
        sprintf(cache_tmp.cache_obj + cache_tmp.cache_len, "%s", buf);
        cache_tmp.cache_len += n;
        Rio_writen(clientfd, buf, n);
    }

    // 更新cache，注意写锁
    pthread_rwlock_wrlock(&cache_rwlock);
    int oldest_idx = 0;
    int oldest_lr_counter = 1024;
    int i = 0;
    for (; i < CACHE_OBJS_COUNT; i++) {
        if (cache_vec[i].is_valid) {
            // 记录最旧cache索引
            if (cache_vec[i].lr_counter < oldest_lr_counter) {
                oldest_lr_counter = cache_vec[i].lr_counter;
                oldest_idx = i;
            }
            continue;
        }else {
            cache_vec[i] = cache_tmp;
            break;
        }
    }
    if (i == CACHE_OBJS_COUNT) {
        cache_vec[oldest_idx] = cache_tmp;
    }
    pthread_rwlock_unlock(&cache_rwlock);

    Close(serverfd);


}


int parse_http_url(char *url, char *host, char *port, char *uri) {
    char *host_ptr;
    char *port_ptr;
    char *path_ptr;

    // 1. 检查并跳过 "http://"
    if (strstr(url, "http://") == url) {
        host_ptr = url + 7; // 指向 "http://" 之后的位置
    } else {
        host_ptr = url;     // 如果没有 http://，直接从头开始解析
    }

    // 2. 找到 host 后面第一个斜杠 '/'，它代表真正的资源路径 (uri) 的起点
    path_ptr = strchr(host_ptr, '/');
    if (path_ptr != NULL) {
        // 把路径内容拷贝到 uri 变量中
        strcpy(uri, path_ptr);

        // 【关键技巧】临时把这个 '/' 替换成 '\0'（字符串结束符）
        // 这样就把整个 url 字符串从这里斩断了，方便后面只在 host:port 部分做文章
        *path_ptr = '\0';
    } else {
        // 如果 URL 里根本没有斜杠（例如 http://localhost:8903），那默认路径就是根目录 "/"
        strcpy(uri, "/");
    }

    // 3. 在剩下的 host:port 字符串中，查找是否有冒号 ':'
    port_ptr = strchr(host_ptr, ':');
    if (port_ptr != NULL) {
        // 如果有冒号，说明指定了端口
        *port_ptr = '\0';          // 再次用 '\0' 斩断，前半部分就是纯 host
        strcpy(host, host_ptr);
        strcpy(port, port_ptr + 1); // 冒号后面就是 port
    } else {
        // 如果没有冒号，说明使用的是标准 HTTP 默认端口 80
        strcpy(host, host_ptr);
        strcpy(port, "80");        // 必须强制补上默认端口，因为 open_clientfd 需要它
    }

    // 4. 【善后工作】如果刚才把 path_ptr 处的 '/' 改成了 '\0'，记得还原它
    // 虽然后面用不到了，但保护传入参数不被永久破坏是优秀打码人的好习惯
    if (path_ptr != NULL) {
        *path_ptr = '/';
    }

    // 如果刚才切出了冒号，也还原它
    if (port_ptr != NULL) {
        *port_ptr = ':';
    }

    return 0;
}