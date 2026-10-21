#include <stdio.h>
#include "csapp.h"
#include "cache.h"
#include "sbuf.h"

#define NTHREADS 4
#define SBUFSIZE 16
sbuf_t sbuf; /* Shared buffer of connected descriptors */

struct Uri
{
    char host[MAXLINE]; // hostname
    char port[MAXLINE]; // 端口
    char path[MAXLINE]; // 路径
};

void doit(int fd);
void parse_requesthdrs(rio_t *rp, char *header_forward, char *host);
void parse_uri(char *uri, struct Uri *uri_data);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* $begin proxy main */
// 新线程的处理逻辑
void *thread(void *vargp)
{
    Pthread_detach(pthread_self()); // 线程分离

    while (1) // 循环处理，获取描述符缓存区中的连接描述符，然后读取客户端请求并响应
    {
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */ // 从描述符缓冲区中获取一个连接描述符
        doit(connfd); /* Service client */                               // 响应客户端请求
        Close(connfd);                                                   // 关闭连接
    }
}
// 传入参数为端口号，初始化代理服务器，等待接收客户端连接请求
int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN); // 捕获并忽略SIGPIPE信号

    cache_init();

    listenfd = Open_listenfd(argv[1]); // 服务器根据端口号打开监听描述符

    sbuf_init(&sbuf, SBUFSIZE); // 初始化生产消费者模式缓冲区
    // 创建多个线程并发等待获取缓冲区中的连接描述符
    pthread_t tid;
    for (int i = 0; i < NTHREADS; i++) /* Create worker threads */
        Pthread_create(&tid, NULL, thread, NULL);
    while (1)
    { // 循环等待客户端连接
        clientlen = sizeof(clientaddr);
        // 接收客户端连接请求
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        // 获取客户端的ip和端口
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        // 将连接描述符插入到描述符缓冲区中
        sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
    }
}
/* $end proxy main */

/*
 * doit - handle one HTTP request/forward/response transaction
 */
/* $begin doit */
// 传入参数为与客户端的连接描述符
void doit(int client_fd)
{
    // 从客户端读取、转发给服务器，从服务器读取、回复给客户端所用的缓冲区
    char buf[MAXLINE];
    // 从客户端读取的请求的解析信息
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    // 转发给服务器的请求头信息
    char header_forward[MAXLINE];
    // 分别与客户端和服务器连接时的读缓冲区
    rio_t client_rio, server_rio;

    /* Read request line and headers */
    // 初始化读缓冲区，并与连接描述符绑定
    Rio_readinitb(&client_rio, client_fd);
    // 读取和解析请求行
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;
    printf("%s", buf); // 打印读取的请求行

    // 当终端用户在web浏览器的地址栏中输入URL(例如http://www.cmu.edu/hub/index.html)时
    // 浏览器将向代理发送一个HTTP请求，该请求以一行开头
    // 例如：GET http://www.cmu.edu/hub/index.html HTTP/1.1
    // 从请求行中解析出请求方法、uri以及HTTP版本
    sscanf(buf, "%s %s %s", method, uri, version);

    // 判断请求方法，这里只对GET方法进行处理
    if (strcasecmp(method, "GET"))
    {
        printf("Tiny does not implement this method");
        return;
    }

    char cache_url[MAXLINE];
    strcpy(cache_url, uri);
    // 检查当前uri对应的回复是否已被缓存，如果命中缓存，直接返回
    if (find_cache(&client_rio, uri))
    {
        return;
    }

    struct Uri *uri_data = (struct Uri *)malloc(sizeof(struct Uri));
    /* Parse URI from GET request */
    // 将uri(https://hostname:port/path)解析到uri_data结构体中
    parse_uri(uri, uri_data);

    // 读取请求头并构建转发请求头
    parse_requesthdrs(&client_rio, header_forward, uri_data->host);

    // 连接服务器
    int server_fd = Open_clientfd(uri_data->host, uri_data->port);
    if (server_fd < 0)
    {
        printf("connection failed\n");
        return;
    }

    // 转发给服务器
    Rio_readinitb(&server_rio, server_fd);
    sprintf(buf, "GET %s HTTP/1.0\r\n%s", uri_data->path, header_forward);
    // 发送请求行和请求头
    if (rio_writen(server_fd, buf, strlen(buf)) != strlen(buf))
    {
        fprintf(stderr, "Send request line and header error\n");
        close(server_fd);
        return;
    }

    char cache_buf[MAX_OBJECT_SIZE];
    int tot_cnt = 0;
    size_t n;
    // 回复给客户端
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
    {
        tot_cnt += n;
        // 若回复的内容长度小于最大缓存长度，则累加到缓冲区中
        if (tot_cnt < MAX_OBJECT_SIZE)
        {
            strcat(cache_buf, buf);
        }
        printf("proxy received %d bytes,then send\n", (int)n);
        Rio_writen(client_fd, buf, n);
    }
    // 若回复的内容长度小于最大缓存长度，则将缓冲区累加的内容添加到缓存
    if (tot_cnt < MAX_OBJECT_SIZE)
    {
        insert_cache(cache_url, cache_buf, tot_cnt);
    }
    // 关闭服务器描述符
    Close(server_fd);
}
/* $end doit */

/* $begin parse_uri */
// 传入uri字符串，解析uri，将解析到的hostname、port、path赋值到uri_data结构体中
// https://hostname:port/path
void parse_uri(char *uri, struct Uri *uri_data)
{
    if (strstr(uri, "http://") != uri)
    {
        fprintf(stderr, "only support https protocol!\n");
        exit(0);
    }
    char *hostbase = strstr(uri, "//"); // "//"之后为hostname:port/path

    if (hostbase == NULL) // 没有hostname和端口号，设置默认端口为 80
    {
        char *pathbase = strstr(uri, "/"); // "/"之后为path
        if (pathbase != NULL)
            strcpy(uri_data->path, pathbase); // 赋值path
        strcpy(uri_data->port, "80");         // 赋值端口
        return;
    }
    else
    {
        char *portpose = strstr(hostbase, ":"); // ":"之后为端口号
        if (portpose != NULL)
        {
            int tmp;
            // 从字符串":port/path"中解析出端口号和path，赋值给uri_data结构体
            sscanf(portpose + 1, "%d%s", &tmp, uri_data->path);
            sprintf(uri_data->port, "%d", tmp);
            *portpose = '\0'; // 将":"替换为"\0"，以便后面获取hostname
        }
        else // 没有端口号，设置默认端口为 80
        {
            char *pathbase = strstr(hostbase + 2, "/");
            if (pathbase != NULL)
            {
                strcpy(uri_data->path, pathbase); // 赋值path
                strcpy(uri_data->port, "80");     // 赋值端口
                *pathbase = '\0';                 // 将":"替换为"\0"，以便后面获取hostname
            }
        }
        strcpy(uri_data->host, hostbase + 2); // 赋值hostname
    }
    return;
}
/* $end parse_uri */

/* $begin parse_requesthdrs */
// 读请求报头并构建转发请求头
void parse_requesthdrs(rio_t *rp, char *header_forward, char *host)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    // 循环读取直到遇到分隔的空行为止，表示请求报头结束
    // 若请求头中包含Host头，则需要转发请求头中的host，否则转发解析出的host
    int has_host_flag = 0; // 记录是否遇到 Host 头
    while (strcmp(buf, "\r\n"))
    {
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
        // 如果遇到 Host 头，记录之，后续不再添加 Host 头
        if (!strncasecmp(buf, "Host:", strlen("Host:")))
        {
            has_host_flag = 1;
        }
        // 如果遇到 Connection 头、Proxy-Connection 头、User-Agent 头，直接跳过，后续替换为默认值
        if (!strncasecmp(buf, "Connection:", strlen("Connection:")))
        {
            continue;
        }
        if (!strncasecmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")))
        {
            continue;
        }
        if (!strncasecmp(buf, "User-Agent:", strlen("User-Agent:")))
        {
            continue;
        }
        // 其他头与 Host 头直接添加到 header_forward 中
        strcat(header_forward, buf);
    }
    // 如果没有 Host 头，添加解析出的 Host 头
    if (!has_host_flag)
    {
        sprintf(buf, "Host: %s\r\n", host);
        strcat(header_forward, buf);
    }
    // 添加 Connection 头、Proxy-Connection 头、User-Agent 头
    strcat(header_forward, "Connection: close\r\n");
    strcat(header_forward, "Proxy-Connection: close\r\n");
    strcat(header_forward, user_agent_hdr);
    // 添加结束行
    strcat(header_forward, "\r\n");
    return;
}
/* $end parse_requesthdrs */
