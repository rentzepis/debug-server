#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NTHREADS 4   //线程池中线程的数量
#define SBUFSIZE 16  //有界缓冲区的大小

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

//用于存放URL的host、port、path，MAXLINE在csapp.h中定义为8192
typedef struct URL {
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
} URL;

//有界缓冲区的结构体定义
typedef struct {
    int *buf;          /* 缓冲区数组 */         
    int n;             /* 最大槽位数 */
    int front;         /* 头指针 */
    int rear;          /* 尾指针 */
    sem_t mutex;       /* 保护对 buf 的访问 */
    sem_t slots;       /* 统计可用槽位 */
    sem_t items;       /* 统计可用项目 */
} sbuf_t;

sbuf_t sbuf;

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

void parseUrl(char *s, URL *url);//解析URL
void doit(int connfd);//处理HTTP请求 
void readClient(rio_t *rio, URL *url, char *data);//读取并解析客户端请求
void *thread(void *vargp);//线程处理函数
/*
* 创建监听套接字，循环接收请求，创建线程处理请求
* Part1：实现基本功能
* Part2：使用线程池和有界缓冲区，加入初始化生产者-消费者模型，实现多线程并发处理
*/
int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
 
    /* 检查命令行参数 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
 
    Signal(SIGPIPE, SIG_IGN); // 忽略 SIGPIPE 信号，防止写入关闭的连接导致进程退出
    sbuf_init(&sbuf, SBUFSIZE);
    // 初始化线程池，创建 NTHREADS 个工作线程
    for (int i = 0; i < NTHREADS; ++i)
        Pthread_create(&tid, NULL, thread, NULL);
 
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        // 等待并接受客户端连接请求
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        // 将连接描述符插入到有界缓冲区，交给工作线程处理
        sbuf_insert(&sbuf, connfd);
    }
}

void parseUrl(char *s, URL *url) {
    //读取https://或http://后面的信息
    char *ptr = strstr(s, "//");
    if (ptr != NULL) s = ptr + 2;
 
    strcpy(url->path, "/"); // 默认路径为 "/"，防止 URL 中没有路径的情况
    // 查找路径开始的位置 '/'
    ptr = strchr(s, '/');
    if (ptr != NULL) {
        strncpy(url->path, ptr, MAXLINE - 1);
        url->path[MAXLINE - 1] = '\0';
        *ptr = '\0'; // 截断字符串，方便后续提取 host 和 port
    }
    
    // 查找端口开始的位置 ':'
    ptr = strchr(s, ':');
    if (ptr != NULL) {
        strncpy(url->port, ptr + 1, MAXLINE - 1);
        url->port[MAXLINE - 1] = '\0';
        *ptr = '\0'; // 截断字符串，剩下的就是 host
    } else strcpy(url->port, "80"); // 默认端口 80
 
    strncpy(url->host, s, MAXLINE - 1);
    url->host[MAXLINE - 1] = '\0';
}

void doit(int connfd) {
    rio_t rio;
    char line[MAXLINE];
    rio_readinitb(&rio, connfd);
    
    URL url;
    char data[MAXLINE];
    data[0] = '\0';
    // 读取并解析客户端的 HTTP 请求
    readClient(&rio, &url, data);
 
    if (strlen(data) == 0) return;

    // 建立与目标服务器的连接
    int serverfd = open_clientfd(url.host, url.port);
    if (serverfd < 0) {
        printf("Connection failed!\n");
        return;
    }
    
    // 将构建好的 HTTP 请求发送给目标服务器
    if (rio_writen(serverfd, data, strlen(data)) != strlen(data)) {
        Close(serverfd);
        return;
    }
    
    // 读取目标服务器的响应，并转发给客户端
    // 使用 read 而不是 rio_readlineb，以支持二进制数据的高效转发
    int n;
    while ((n = read(serverfd, line, MAXLINE)) > 0) {
        if (rio_writen(connfd, line, n) != n) break;
    }
    
    Close(serverfd);
}

void readClient(rio_t *rio, URL *url, char *data) {
    char host[MAXLINE * 2];
    char line[MAXLINE];
    char other[MAXLINE];
    char method[MAXLINE], urlstr[MAXLINE], version[MAXLINE];
    
    other[0] = '\0'; 

    if (rio_readlineb(rio, line, MAXLINE) <= 0) return;
    if (sscanf(line, "%s %s %s\n", method, urlstr, version) != 3) return;
    parseUrl(urlstr, url);
 
    snprintf(host, sizeof(host), "Host: %s\r\n", url->host);
    while (rio_readlineb(rio, line, MAXLINE) > 0) {
        if (strcmp(line, "\r\n") == 0) break;
        if (strncmp(line, "Host", 4) == 0) strcpy(host, line);
        if (strncmp(line, "User-Agent", 10) &&
            strncmp(line, "Connection", 10) &&
            strncmp(line, "Proxy-Connection", 16)) strncat(other, line, MAXLINE - strlen(other) - 1);
    }
    
    sprintf(data, "%s %s HTTP/1.0\r\n"
                     "%s%s"
                     "Connection: close\r\n"
                     "Proxy-Connection: close\r\n"
                     "%s\r\n", method, url->path, host, user_agent_hdr, other);
}

void *thread(void *vargp) {
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
    return NULL;
}

//以上是Part1的代码，以下是Part2的代码

//创建并初始化有界缓冲区
void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int)); 
    sp->n = n;                       /* 缓冲区最多容纳 n 个项目 */
    sp->front = sp->rear = 0;        /* 当 front == rear 时缓冲区为空 */
    Sem_init(&sp->mutex, 0, 1);      /* 用于锁定的二元信号量 */
    Sem_init(&sp->slots, 0, n);      /* 初始时，buf 有 n 个空槽位 */
    Sem_init(&sp->items, 0, 0);      /* 初始时，buf 有 0 个数据项 */
}


/* 清理缓冲区  */
void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

/* 插入到共享缓冲区的尾部 */
void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);                          /* 等待可用槽位 */
    P(&sp->mutex);                          /* 锁定缓冲区 */
    sp->buf[(++sp->rear)%(sp->n)] = item;   /* 插入项目 */
    V(&sp->mutex);                          /* 解锁缓冲区 */
    V(&sp->items);                          /* 宣布有可用项目 */
}

/* 从缓冲区 sp 移除并返回第一个项目 */
int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);                          /* 等待可用项目 */
    P(&sp->mutex);                          /* 锁定缓冲区 */
    item = sp->buf[(++sp->front)%(sp->n)];  /* 移除项目 */
    V(&sp->mutex);                          /* 解锁缓冲区 */
    V(&sp->slots);                          /* 宣布有可用槽位 */
    return item;
}