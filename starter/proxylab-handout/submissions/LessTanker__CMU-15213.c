#include <stdio.h>
#include <pthread.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void *thread(void *vargp);
void doit(int connfd);
void parse_uri(char *uri, char *hostname, char *pathname, int *port);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
    int listenfd;
    int *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);

        /* 为每个连接分配独立的存储，避免竞争 */
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        /* 取得对端信息（可选，仅用于调试输出） */
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        pthread_t tid;
        Pthread_create(&tid, NULL, thread, connfdp); /* 新线程处理连接 */
    }
    return 0;
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Free(vargp);                      /* 释放 main 分配的内存 */
    Pthread_detach(pthread_self());   /* 分离线程，避免资源泄漏 */
    doit(connfd);                     /* 处理请求并把响应回写到 connfd */
    Close(connfd);                    /* 统一在这里关闭客户端连接 */
    return NULL;
}

void doit(int connfd)
{
    char buf[MAXLINE]; /* 用于逐行读取客户端发来的头部 */
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE];
    rio_t rio_client, rio_server;
    int port;           /* 目标服务器端口 */
    int serverfd;

    Rio_readinitb(&rio_client, connfd);

    /* 读取请求行 */
    if (Rio_readlineb(&rio_client, buf, MAXLINE) <= 0)
        return;
    printf("Request line: %s", buf);

    sscanf(buf, "%s %s %s", method, uri, version);

    /* 仅处理 GET */
    if (strcasecmp(method, "GET")) {
        printf("Proxy only handles GET\n");
        return;
    }

    /* 解析 URI -> hostname, pathname, port */
    parse_uri(uri, hostname, pathname, &port);

    /* 连接到目标服务器 */
    char port_str[10];
    snprintf(port_str, sizeof(port_str), "%d", port);
    serverfd = Open_clientfd(hostname, port_str);
    if (serverfd < 0) {
        printf("Connection failed to %s:%d\n", hostname, port);
        return; /* 让线程末尾关闭 connfd */
    }
    Rio_readinitb(&rio_server, serverfd);

    /* 构造 HTTP/1.0 请求：完整包含结尾的空行 */
    char new_request[MAXLINE];
    snprintf(new_request, sizeof(new_request),
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "%s"
             "Connection: close\r\n"
             "Proxy-Connection: close\r\n"
             "\r\n",
             pathname, hostname, user_agent_hdr);

    /* 转发客户端除了 Host/User-Agent/Connection/Proxy-Connection 以外的 header */
    while (Rio_readlineb(&rio_client, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0) break; /* header 结束 */
        if (strncasecmp(buf, "Host:", 5) == 0) continue;
        if (strncasecmp(buf, "User-Agent:", 11) == 0) continue;
        if (strncasecmp(buf, "Connection:", 11) == 0) continue;
        if (strncasecmp(buf, "Proxy-Connection:", 17) == 0) continue;

        /* 注意：new_request 预留 MAXLINE，如果 header 太多可能溢出——手动检查更稳妥 */
        if (strlen(new_request) + strlen(buf) < sizeof(new_request) - 1)
            strcat(new_request, buf);
    }

    /* 发送到目标服务器 */
    Rio_writen(serverfd, new_request, strlen(new_request));

    /* 从目标服务器读取响应并写回给客户端（处理二进制数据） */
    ssize_t n;
    while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0)
        Rio_writen(connfd, buf, n);

    Close(serverfd);
}

/* 更鲁棒的 parse_uri 实现 */
void parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    *port = 80; /* 默认端口 */

    char *host_ptr = strstr(uri, "//");
    host_ptr = (host_ptr != NULL) ? host_ptr + 2 : uri;

    /* 找出 path 部分 */
    char *path_ptr = strchr(host_ptr, '/');
    if (path_ptr) {
        strcpy(pathname, path_ptr);
        *path_ptr = '\0'; /* 临时截断 host 部分，便于下步处理 port/host */
    } else {
        strcpy(pathname, "/");
    }

    /* 检查是否有 :port */
    char *port_ptr = strchr(host_ptr, ':');
    if (port_ptr) {
        *port_ptr = '\0';
        *port = atoi(port_ptr + 1);
    }

    strcpy(hostname, host_ptr);
}
