/*
 * proxy.c - ICS Web proxy
 * 赵楷越 522031910803
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

/*
 * Function prototypes
 */

// 线程的入口函数，参数是一个指向 thread_arg 结构体的指针
void *thread_function(void *thread_arg);

// 处理连接的doit函数
void doit(int connfd, struct sockaddr_in *clientaddr);

// 处理连接的send和receive函数
int send_request(int clientfd, rio_t *conn_rio, char *req_header, size_t length, char *method);
size_t receive_request(int connfd, rio_t *client_rio);

// 解析uri，提取主机名、路径名和端口号
int parse_uri(char *uri, char *target_addr, char *path, char *port);

// 日志函数
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

// RIO包装函数
int Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);

// 为了方便地传入线程的入口函数，创建用于存储连接的地址和套接字文件描述符的结构体
struct arg_thread
{
    int connfd;
    struct sockaddr_in clientaddr;
};

// 信号量，用于保护对服务器描述符的访问，
sem_t m_log;

// 主函数，创建监听套接字，循环接受连接
int main(int argc, char **argv)
{
    // 初始化变量
    int listenfd = Open_listenfd(argv[1]);
    socklen_t clientlen = sizeof(struct sockaddr_in);
    pthread_t tid;
    struct arg_thread *thread_arg;

    // 从命令行参数中获取端口号
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    // 初始化信号量
    Sem_init(&m_log, 0, 1);

    // To keep your proxy from crashing you can use the SIG IGN argument to the
    // signal function (CS:APP 8.5.3) to explicitly ignore these SIGPIPE signals
    Signal(SIGPIPE, SIG_IGN);

    // 循环接受连接
    while (1)
    {
        thread_arg = Malloc(sizeof(struct arg_thread));
        thread_arg->connfd = Accept(listenfd, (SA *)&(thread_arg->clientaddr), &clientlen);
        Pthread_create(&tid, NULL, &thread_function, thread_arg);
    }

    // 关闭监听套接字
    Close(listenfd);
    exit(1);
}

// 线程的入口函数
void *thread_function(void *thread_arg)
{
    // To avoid a potentially fatal memory leak,
    // your threads should run as detached, not joinable.
    Pthread_detach(Pthread_self());

    // doit
    struct arg_thread *arg_self = (struct arg_thread *)thread_arg;
    doit(arg_self->connfd, &(arg_self->clientaddr));

    // 关闭连接
    Close(arg_self->connfd);
    Free(arg_self);
    return NULL;
}

// 处理连接的doit函数
void doit(int connfd, struct sockaddr_in *clientaddr)
{
    // 初始化变量
    char buf[MAXLINE];
    char req_header[MAXLINE * 2];
    char method[MAXLINE / 4];
    char uri[MAXLINE];
    char version[MAXLINE / 2];
    char hostname[MAXLINE];
    char pathname[MAXLINE];
    char port[MAXLINE];
    int clientfd;
    rio_t conn_rio, client_rio;
    size_t byte_size = 0, content_length = 0;

    // 读取请求行
    Rio_readinitb(&conn_rio, connfd);
    if (!Rio_readlineb_w(&conn_rio, buf, MAXLINE))
    {
        fprintf(stderr, "error: read empty request line\n");
        return;
    }
    if (sscanf(buf, "%s %s %s", method, uri, version) < 3)
    {
        fprintf(stderr, "error: mismatched parameters\n");
        return;
    }
    if (parse_uri(uri, hostname, pathname, port) != 0)
    {
        fprintf(stderr, "error: parse uri error\n");
        return;
    }

    // 写入请求头
    sprintf(req_header, "%s /%s %s\r\n", method, pathname, version);
    size_t n = Rio_readlineb_w(&conn_rio, buf, MAXLINE);
    char tmp_header[MAXLINE];
    while (n != 0)
    {
        if (strncasecmp(buf, "Content-Length", 14) == 0)
        {
            sscanf(buf + 15, "%zu", &content_length);
        }
        strcpy(tmp_header, req_header);
        sprintf(req_header, "%s%s", tmp_header, buf);

        if (strncmp(buf, "\r\n", 2) == 0)
            break;

        n = Rio_readlineb_w(&conn_rio, buf, MAXLINE);
    }

    // 如果没有内容，直接返回
    if (n == 0)
    {
        return;
    }

    clientfd = open_clientfd(hostname, port);
    if (clientfd < 0)
    {
        fprintf(stderr, "error: open client fd error (hostname: %s, port: %s)\n", hostname, port);
        return;
    }
    Rio_readinitb(&client_rio, clientfd);

    // 发送请求到服务器，接收服务器的响应
    if (send_request(clientfd, &conn_rio, req_header, content_length, method) == 0)
    {
        byte_size = receive_request(connfd, &client_rio);
    }

    // 记录日志
    format_log_entry(buf, clientaddr, uri, byte_size);

    // 这里要保护对日志的打印，所以要加锁
    P(&m_log);
    printf("%s\n", buf);
    V(&m_log);

    Close(clientfd);
}

// 处理连接的send函数，发送请求到服务器
int send_request(int clientfd, rio_t *conn_rio, char *req_header, size_t length, char *method)
{
    char buf[MAXLINE];

    // 写入请求头
    if (Rio_writen_w(clientfd, req_header, strlen(req_header)) == -1)
    {
        return -1;
    }

    // 写入请求体
    if (strcasecmp(method, "GET") != 0)
    {
        for (int i = 0; i < length; ++i)
        {
            if (Rio_readnb_w(conn_rio, buf, 1) == 0)
                return -1;
            if (Rio_writen_w(clientfd, buf, 1) == -1)
                return -1;
        }
    }

    return 0;
}

// 处理连接的receive函数，接收服务器的响应
size_t receive_request(int connfd, rio_t *client_rio)
{
    char buf[MAXLINE];
    size_t byte_size = 0, content_length = 0;

    // 读取响应头
    size_t n = Rio_readlineb_w(client_rio, buf, MAXLINE);
    while (n != 0)
    {
        byte_size += n;
        if (strncasecmp(buf, "Content-Length", 14) == 0)
        {
            sscanf(buf + 15, "%zu", &content_length);
        }
        if (Rio_writen_w(connfd, buf, strlen(buf)) == -1)
        {
            return 0;
        }

        if (strncmp(buf, "\r\n", 2) == 0)
            break;

        n = Rio_readlineb_w(client_rio, buf, MAXLINE);
    }

    // 如果没有内容，直接返回
    if (n == 0)
    {
        return 0;
    }

    // 读取响应体
    for (int i = 0; i < content_length; ++i)
    {
        if (Rio_readnb_w(client_rio, buf, 1) == 0)
            return 0;
        if (Rio_writen_w(connfd, buf, 1) == -1)
            return 0;
        ++byte_size;
    }

    return byte_size;
}

/*
 * The Rio readn, Rio readlineb, and Rio writen error checking wrappers in csapp.c are not
 * appropriate for a realistic proxy because they terminate the process when they encounter an
 * error. Instead, you should write new wrappers called Rio readn w, Rio readlineb w, and Rio
 * writen w that simply return after printing a warning message when I/O fails. When either
 * of the read wrappers detects an error, it should return 0, as though it encountered EOF on the socket.
 */
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
    {
        fprintf(stderr, "Rio_readnb error\n");
        return 0;
    }

    return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
    {
        fprintf(stderr, "Rio_readlineb error\n");
        return 0;
    }

    return rc;
}

int Rio_writen_w(int fd, void *usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != n)
    {
        fprintf(stderr, "Rio_writen error\n");
        return -1;
    }

    return 0;
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0)
    {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':')
    {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    }
    else
    {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL)
    {
        pathname[0] = '\0';
    }
    else
    {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size)
{
    time_t now;
    char time_str[MAXLINE];
    char host[INET_ADDRSTRLEN];

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    if (inet_ntop(AF_INET, &sockaddr->sin_addr, host, sizeof(host)) == NULL)
        unix_error("Convert sockaddr_in to string representation failed\n");

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %s %s %zu", time_str, host, uri, size);
}