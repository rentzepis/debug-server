#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *user_connect_hdr = "Connection: close\r\n";
static const char *user_proxy_hdr = "Proxy-Connection: close\r\n";


typedef struct {
    char hostname[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
}URI;


void doit(int fd);
int read_requesthdrs(rio_t *rp);
void parse_uri(char *uri, URI *uri_data);
void build_header(char *server, URI* uri_data, rio_t *rio);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	clientlen = sizeof(clientaddr);
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
	doit(connfd);                                             //line:netp:tiny:doit
	Close(connfd);                                            //line:netp:tiny:close
    }
}
/* $end tinymain */

/*
 * doit - receive and send one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int is_static;  //表示是否为静态内容
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char server[MAXLINE];  //转发给server的数据
    rio_t rio, server_rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);  // connfd是用户与proxy进行connect的套接字
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;  //从客户端connfd读取一行数据，如果没有输入则返回
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        printf("Proxy doesn't implement this method\n");
        return ;  //确保只运行GET事务
    }                                                    //line:netp:doit:endrequesterr

    // 解析uri
    URI *uri_data=Malloc(sizeof(URI));
    parse_uri(uri, uri_data);

    // 设置header
    build_header(server, uri_data, &rio);  //从用户connfd套接字读取数据

    // 连接服务器
    int serverfd = Open_clientfd(uri_data->hostname, uri_data->port);
    if(serverfd < 0){
        printf("Connected fail\n");
        return ;
    }

    // 转发给服务器
    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, server, strlen(server));

    // 回复给客户端
    size_t n;
    while((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0){
        printf("Proxy received %d bytes, then send\n", (int)n);
        Rio_writen(fd, buf, n);
    }

    // 关闭与server连接的proxy自己的描述符/文件句柄
    Close(serverfd);

    Free(uri_data);
}
/* $end doit */

/* $begin parse_uri */
void parse_uri(char *uri, URI *uri_data) 
{
    memset(uri_data, 0, sizeof(uri_data));
    char *hostname = strstr(uri, "//");
    if(!hostname){
        // 没找到主机名
        char *path = strstr(uri, "/");
        if(path){
            strcpy(uri_data->path, path);
        }
        return ;
    }
    else{
        // 找到主机名
        char *port = strstr(hostname+2, ":");
        if(port){
            int tmp;
            sscanf(port+1, "%d %s", &tmp, uri_data->path);
            sprintf(uri_data->port, "%d", tmp);
            *port = '\0';
        }
        else{
            char *path = strstr(hostname+2, "/");
            if(path){
                strcpy(uri_data->path, path);
                strcpy(uri_data->port, "80");
                *path = '\0';
            }
        }
        strcpy(uri_data->hostname, hostname+2);
    }
    return ;
}
/* $end parse_uri */

// 拼接成一个完整的http请求至server中
void build_header(char *server, URI* uri_data, rio_t *rio){
    char req_line[MAXLINE], host_hdr[MAXLINE];

    // GET请求行
    snprintf(req_line, MAXLINE, "GET %s HTTP/1.0\r\n", uri_data->path);

    // Host请求头
    snprintf(host_hdr, MAXLINE, "Host: %s\r\n", uri_data->hostname);

    sprintf(server, "%s%s%s%s%s\r\n", req_line, host_hdr, user_agent_hdr, user_connect_hdr, user_proxy_hdr);
}