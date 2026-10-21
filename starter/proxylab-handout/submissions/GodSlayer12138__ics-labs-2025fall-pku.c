#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void build_http_header(char *http_header, char *hostname, char *path, char *port, rio_t *client_rio);
void *thread(void *vargp);

int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    cache_init();
    Signal(SIGPIPE, SIG_IGN); // 忽略 SIGPIPE 信号，防止写入已关闭的 socket 时进程退出

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int)); // 为每个连接分配独立的内存，避免竞争条件
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp); // 创建新线程处理请求
    }

    Close(listenfd);
    return 0;
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self()); // 分离线程，使其资源在终止时自动回收
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int fd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char http_header[MAXLINE];
    char cache_buf[MAX_OBJECT_SIZE];
    int cache_len = 0;
    rio_t rio, server_rio;
    int serverfd;
    size_t n;

    rio_readinitb(&rio, fd);
    // 读取请求行
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        return;
    }

    // 解析请求行
    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        return;
    }
    
    // 检查缓存
    char url_store[MAXLINE];
    strcpy(url_store, uri);
    if (read_cache(url_store, fd)) {
        return;
    }

    // 解析 URI，提取主机名、路径和端口
    parse_uri(uri, hostname, path, port);

    // 构建发送给服务器的 HTTP 头
    build_http_header(http_header, hostname, path, port, &rio);

    // 连接到目标服务器
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        return;
    }

    rio_readinitb(&server_rio, serverfd);
    // 发送请求头到服务器
    if (rio_writen(serverfd, http_header, strlen(http_header)) != strlen(http_header)) {
        Close(serverfd);
        return;
    }

    // 转发服务器响应给客户端，并尝试缓存
    while ((n = rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        if (rio_writen(fd, buf, n) != n) {
            break;
        }
        if (cache_len + n <= MAX_OBJECT_SIZE) {
            memcpy(cache_buf + cache_len, buf, n);
            cache_len += n;
        } else {
            cache_len = MAX_OBJECT_SIZE + 1;
        }
    }

    if (cache_len <= MAX_OBJECT_SIZE) {
        write_cache(url_store, cache_buf, cache_len);
    }

    Close(serverfd);
}

void parse_uri(char *uri, char *hostname, char *path, char *port)
{
    char *ptr;

    if (strstr(uri, "http://")) {
        uri = uri + 7;
    }

    ptr = strchr(uri, '/');
    if (ptr) {
        strcpy(path, ptr);
        *ptr = '\0';
        strcpy(hostname, uri);
    } else {
        strcpy(path, "/");
        strcpy(hostname, uri);
    }

    ptr = strchr(hostname, ':');
    if (ptr) {
        *ptr = '\0';
        strcpy(port, ptr + 1);
    } else {
        strcpy(port, "80");
    }
}

void build_http_header(char *http_header, char *hostname, char *path, char *port, rio_t *client_rio)
{
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
    
    host_hdr[0] = '\0';
    other_hdr[0] = '\0';

    sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);

    while(rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if(strcmp(buf, "\r\n") == 0) break;

        if(!strncasecmp(buf, "Host", 4)) {
            strcpy(host_hdr, buf);
            continue;
        }

        if(strncasecmp(buf, "Connection", 10) 
           && strncasecmp(buf, "Proxy-Connection", 16) 
           && strncasecmp(buf, "User-Agent", 10)) {
            strcat(other_hdr, buf);
        }
    }

    if(strlen(host_hdr) == 0) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }

    sprintf(http_header, "%s%s%s%s%s%s\r\n", 
            request_hdr,
            host_hdr,
            "Connection: close\r\n",
            "Proxy-Connection: close\r\n",
            user_agent_hdr,
            other_hdr);
}
