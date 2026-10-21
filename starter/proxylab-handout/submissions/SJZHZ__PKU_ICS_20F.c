//1900017702 朱越

//从tiny修改而来
//main函数建立对等线程，分离并执行doit
//差别比较大的部分是需要解析域名、端口号
//重新写doit和parse


#include "csapp.h"
#include <stdio.h>
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
//这一行可以超过80个字
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr = "Host: www.cmu.edu\r\n";



void* thread(void* vargp);//交给对等线程执行
void doit(int fd);//处理一个HTTP请求/响应事务
int parse_uri(char *uri, char *filename, char *cgiargs);//将URI解析为文件名和CGI参数
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg);//向客户端返回错误消息
void build_headers(rio_t *rp, char *newheader, char *hostname, char* port);

void sigchld_handler(int sig)//回收所有子进程
{
    int bkp_errno = errno;
    while(waitpid(-1, NULL, WNOHANG)>0);
    errno=bkp_errno;
}
void* thread(void* vargp)
{
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());//大写or小写？
    free(vargp);
    doit(connfd);
    close(connfd);
    return NULL;
}
int main(int argc, char **argv) 
{
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);
    pthread_t tid;
    int listenfd, connfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2)//检查命令行参数合法性
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        connfdp = malloc(sizeof(int));
        *connfdp = connfd;//线程传参
        getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        //printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

//在Tiny中，程序作为服务器，doit分成动态的和静态的来响应
//在Proxy中，程序不关心内容是什么，直接原样转发
void doit(int client_fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char newheader[MAXLINE], hostname[MAXLINE], path[MAXLINE];
    rio_t rio_server, rio_client;//代理介于服务器和客户端之间，需要两个描述符
    int port, server_fd, n;

//获取method,uri,version
    rio_readinitb(&rio_client, client_fd);//文件初始化
    if (!rio_readlineb(&rio_client, buf, MAXLINE))
        return;
    //printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
//method错误检查
    if (strcasecmp(method, "GET"))//501
    {
        clienterror(client_fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }
//解析URI
    port = parse_uri(uri, hostname, path);//uri->hostname/port/path
    char port_str[10];//把数字转化为字符串比较方便
    sprintf(port_str, "%d", port);

    server_fd = Open_clientfd(hostname, port_str);//建立和服务器的连接
    Rio_readinitb(&rio_server, server_fd);//文件初始化
    sprintf(newheader, "GET %s HTTP/1.0\r\n", path);//转发请求
    build_headers(&rio_client, newheader, hostname, port_str);
    Rio_writen(server_fd, newheader, strlen(newheader));

    //只要服务器有回应，就原样发给客户端
    while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)))
        Rio_writen(client_fd, buf, n);
    close(server_fd);

}
//从uri解析服务器名和路径
//返回值为端口号，如果没有指定则为80
int parse_uri(char *uri, char *hostname, char *path)
{
    int port = 80;
    char* pos1 = uri, *pos2;
    for (char *pos0 = uri; *pos0 != 0; pos0++)//寻找域名
    {
        if (pos0 == uri)
            continue;
        if (pos0 == uri + 1)
            continue;
        if (*pos0 == '/' && *(pos0 - 1) == '/')
        {
            pos1 = pos0 + 1;//跳过两个斜杠
            break;
        }
    }
    for (pos2 = pos1; *pos2 != 0; pos2++)//寻找端口号
    {
        if (*pos2 == ':')
            break;
    }
    if (*pos2 == ':')//指定了端口号
    {
        *pos2 = 0;//冒号改成分隔符
        pos2++;
        strcpy(hostname,pos1);
        sscanf(pos2,"%d%s", &port, path);
        *pos2 = ':';//注意要保持uri不变
    }
    else//未指定端口号
    {
        for (pos2 = pos1; *pos2 != 0; pos2++)//寻找路径名
        {
            if (*pos2 == '/')
                break;
        }
        if (*pos2 == 0)//没有路径名
        {
            strcpy(hostname,pos1);
            strcpy(path,"");
            return port;
        }
        *pos2 = 0;
        strcpy(hostname,pos1);
        *pos2 = '/';
        strcpy(path,pos2);
    }
    return port;
}

//生成新的报头
void build_headers(rio_t *rp, char *newheader, char *hostname, char* port)
{

    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);

    while(strcmp(buf, "\r\n"))
    {
        Rio_readlineb(rp, buf, MAXLINE);
    }
    sprintf(newheader, "%sHost: %s:%s\r\n",newheader, hostname, port);
    sprintf(newheader, "%s%s%s%s", newheader,
        user_agent_hdr, connection_hdr, proxy_connection_hdr);
    sprintf(newheader,"%s\r\n",newheader);
}


//向客户端返回错误消息
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
