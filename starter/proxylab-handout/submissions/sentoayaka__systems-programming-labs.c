#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAXLINE 8192

/* You won't lose style points for including this long line in your code */
static const string user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef char string[MAXLINE];
typedef struct {
    string host;
    string path;
    string port;
} url_t;

void *thread(void *vargp);
void doit(rio_t *client_Rio, string url);
int parse_url(string uri, url_t *url_info);
int parse_header(rio_t *client_Rio, string header_info, string host);

int main(int argc, char** argv) {
    int listenfd, *connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);

    listenfd = Open_listenfd(argv[1]);

    init_cache();

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (*connfd < 0) {
            fprintf(stderr, "Accept error: %s\n", strerror(errno));
            continue;
        }
        pthread_create(&tid, NULL, thread, connfd);
    }
    close(listenfd);
    return 0;
}
void *thread(void *vargp) {
    pthread_detach(pthread_self());

    int connfd = *((int *)vargp);
    Free(vargp);
    
    rio_t client_Rio;
    Rio_readinitb(&client_Rio, connfd);
    char buf[MAXLINE];
    if (Rio_readlineb(&client_Rio, buf, MAXLINE) <= 0) {
        fprintf(stderr, "Error reading request line: %s\n", strerror(errno));
        Close(connfd);
        return NULL;
    }

    char method[MAXLINE], url[MAXLINE], version[MAXLINE];
    if (sscanf(buf, "%s %s %s", method, url, version) != 3) {
        fprintf(stderr, "Malformed request line: %s\n", buf);
        Close(connfd);
        return NULL;
    }

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method");
        Close(connfd);
        return NULL;
    }

    doit(&client_Rio, url);

    Close(connfd);
    return NULL;
}

void doit(rio_t* client_Rio_p, string url) {
    // 检查是否在缓存中，如果命中缓存，直接返回
    if (query_cache(client_Rio_p, url)) {
        return;
    }
    // 解析 url
    url_t url_info;
    if (parse_url(url, &url_info) < 0) {
        fprintf(stderr, "Parse url error\n");
        return;
    }
    // 解析 header
    string header_info;
    header_info[0] = '\0';
    parse_header(client_Rio_p, header_info, url_info.host);

    // 启动与 host 的链接，不使用包装函数（以防exit退出进程）
    int server_fd = open_clientfd(url_info.host, url_info.port);
    if (server_fd < 0) {
        fprintf(stderr, "Open connect to %s:%s error\n", url_info.host, url_info.port);
        return;
    }

    // 初始化服务端缓冲区 Rio
    rio_t server_Rio;
    Rio_readinitb(&server_Rio, server_fd);

    // 准备请求行和请求头
    char buf[MAXLINE * 2];
    sprintf(buf, "GET %s HTTP/1.0\r\n%s", url_info.path, header_info);

    // 发送请求行和请求头
    if (rio_writen(server_fd, buf, strlen(buf)) != strlen(buf)) {
        fprintf(stderr, "Send request line and header error\n");
        close(server_fd);
        return;
    }

    // 接收响应行
    int resp_total = 0, resp_current = 0;
    char file_cache[MAX_OBJECT_SIZE];
    int client_fd = client_Rio_p->rio_fd;

    // 从服务端读取响应
    // server可能会写多次，所以需要循环读取直至遇到 EOF（即 resp_current == 0）
    while ((resp_current = Rio_readnb(&server_Rio, buf, MAXLINE))) {
        if (resp_current < 0) {
            fprintf(stderr, "Read server response error\n");
            close(server_fd);
            return;
        }
        // 缓存到局部变量 file_cache 中，准备供缓存使用
        if (resp_total + resp_current < MAX_OBJECT_SIZE) {
            memcpy(file_cache + resp_total, buf, resp_current);
        }
        resp_total += resp_current;
        // 发送给客户端
        if (rio_writen(client_fd, buf, resp_current) != resp_current) {
            fprintf(stderr, "Send response to client error\n");
            close(server_fd);
            return;
        }
    }
    // 如果响应小于 MAX_OBJECT_SIZE，缓存到本地
    if (resp_total < MAX_OBJECT_SIZE) {
        add_cache(url, file_cache, resp_total);
    }
    close(server_fd);
    return;
}

int parse_url(string url, url_t* url_info) {
    // 检查是否为 HTTP 协议
    const int http_prefix_len = strlen("http://");
    if (strncasecmp(url, "http://", http_prefix_len)) {
        fprintf(stderr, "Not http protocol: %s\n", url);
        return -1;
    }
    // 检查是否为合法的url
    char* host_start = url + http_prefix_len;
    char* port_start = strchr(host_start, ':');
    char* path_start = strchr(host_start, '/');

    // 非法url
    if (path_start == NULL) {
        strcpy(url_info->host, host_start);
        strcpy(url_info->port, "80");
        strcpy(url_info->path, "/");
    }

    // 没有端口号，设置默认端口为 80
    if (port_start == NULL) {
        *path_start = '\0';
        strcpy(url_info->host, host_start);
        strcpy(url_info->port, "80");
        *path_start = '/';
        strcpy(url_info->path, path_start);
    }

    // 有端口号
    else {
        *port_start = '\0';
        strcpy(url_info->host, host_start);
        *port_start = ':';
        *path_start = '\0';
        strcpy(url_info->port, port_start + 1);
        *path_start = '/';
        strcpy(url_info->path, path_start);
    }

    return 0;
}

int parse_header(rio_t* client_Rio_p, string header_info, string host) {
    char* buf = malloc(MAXLINE);
    int has_host_flag = 0;
    while (1) {
        if (rio_readlineb(client_Rio_p, buf, MAXLINE) <= 0) {
            break;
        }
        // 遇到结束行
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
        // 如果遇到 Host 头，记录之，后续不再添加 Host 头
        if (!strncasecmp(buf, "Host:", strlen("Host:"))) {
            has_host_flag = 1;
        }
        // 如果遇到 Connection 头、Proxy-Connection 头、User-Agent 头，直接跳过，后续替换为默认值
        if (!strncasecmp(buf, "Connection:", strlen("Connection:"))) {
            continue;
        }
        if (!strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:"))) {
            continue;
        }
        if (!strncasecmp(buf, "User-Agent:", strlen("User-Agent:"))) {
            continue;
        }
        // 其他头与 Host 头直接添加
        strcat(header_info, buf);
    }
    // 如果没有 Host 头，添加 Host 头
    if (!has_host_flag) {
        sprintf(buf, "Host: %s\r\n", host);
        strcat(header_info, buf);
    }
    // 添加 Connection 头、Proxy-Connection 头、User-Agent 头
    strcat(header_info, "Connection: close\r\n");
    strcat(header_info, "Proxy-Connection: close\r\n");
    strcat(header_info, user_agent_hdr);
    // 添加结束行
    strcat(header_info, "\r\n");
    free(buf);
    return 0;
}