#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

void read_requesthdrs(rio_t *rio);
int parse_uri(char **uri, char *host, int *port);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void *proxythread(void *fd);

int main(int argc, char **argv)
{
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    init_cache();
    // proxy本地监听
    listenfd = Open_listenfd(argv[1]);
    clientlen = sizeof(clientaddr);
    while (1)
    {
        printf("listening..\n");
        memset(&clientaddr, 0, sizeof(clientaddr));
        int *connfd = (int *)Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:Proxy:accept
        // create threads
        if (connfd)
            Pthread_create(&tid, NULL, proxythread, connfd);

        // doit(*connfd);
    }
    Close(listenfd);
    free_cache();
}
/* $end Proxymain */

/*
 * proxythread - thread routine
 */
void *proxythread(void *fd)
{
    int connfd = *((int *)fd);
    free(fd);
    Pthread_detach(pthread_self());

    doit(connfd);
    return NULL;
}

void doit(int fd)
{
    int if_host, serverfd;
    char port[10], buf[MAXLINE], sendbuf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE - 10], *newuri = uri;
    char url[MAXLINE], data[MAX_OBJECT_SIZE];

    int host_flag = 0, n = 0, size = 0,exceed_flag=0;

    rio_t client_rio, server_rio;

    /* Read request line and headers */
    Rio_readinitb(&client_rio, fd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE)) // line:netp:forward2server:readrequest
        return;
    /* separate hostname from uri*/
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version); // line:netp:forward2server:parserequest
    if (strcasecmp(method, "GET"))
    { // line:netp:doit:beginrequesterr
        printf("501 Not Implemented: %s method\n", method);
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }
    if_host = parse_uri(&newuri, hostname, port); // line:netp:forward2server:staticcheck
    if (if_host == -1)
        return;

    sprintf(sendbuf, "%s %s %s\r\n", method, newuri, "HTTP/1.0");
    Rio_readlineb(&client_rio, buf, MAXLINE);

    while (strcmp(buf, "\r\n"))
    {
        printf("%s", buf);
        if (!host_flag && strstr(buf, "Host:"))
        {
            host_flag = 1;
            if (if_host == 1)
            {
                char *ptr3 = buf + 6;
                char *ptr2 = strstr(ptr3, ":");
                if (ptr2 != NULL)
                {
                    strncpy(hostname, ptr3, ptr2 - ptr3);
                    hostname[ptr2 - ptr3] = '\0';
                    int portNum = atoi(ptr2 + 1);
                    sprintf(port, "%d", portNum);
                    *ptr2 = '\0';
                }
            }
            sprintf(sendbuf, "%s%s", sendbuf, buf);
        }
        else if (!strstr(buf, "User-Agent:") && !strstr(buf, "Proxy-Connection:") && !strstr(buf, "Connection:"))
            sprintf(sendbuf, "%s%s", sendbuf, buf);

        Rio_readlineb(&client_rio, buf, MAXLINE);
    }
    // 检查是否有字段没有出现
    if (!host_flag)
    {
        sprintf(sendbuf, "%sHost: %s\r\n", sendbuf, hostname);
    }
    sprintf(sendbuf, "%s%s%s%s", sendbuf, user_agent_hdr, connection_hdr, proxy_connection_hdr);
    sprintf(sendbuf, "%s\r\n", sendbuf);
    printf("待发送数据\n%s", sendbuf);

    printf("hostname:%s\nport:%s\n", hostname, port);
    // 检查cache
    sprintf(url, "%s%s%s", hostname, port, newuri);
    if (read_cache(url, fd) == 1)
    {
        Close(fd);
        return;
    } // hit cache

    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0)
    {
        printf("open clientfd error\n");
        return;
    }
    Rio_writen(serverfd, sendbuf, strlen(sendbuf));

    Rio_readinitb(&server_rio, serverfd);
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)))
    {                           // real server response to buf
        if(size+n<MAX_OBJECT_SIZE){
            memcpy(data+size,buf,n);
            size+=n;
        }
        else
            exceed_flag=1;
        Rio_writen(fd, buf, n); // real server response to real client
    }
    if(!exceed_flag)
        write_cache(url, data, size);
    Close(fd);
    Close(serverfd);
    return;
}

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 *  probable uri:www.baidu.com/index.html  | localhost:1234/index.html
 */
/* $begin parse_uri */
int parse_uri(char **uri, char *host, int *port)
{
    char *head = *uri;
    if ((*uri)[0] == '/') // 浏览器已经将主机名剥离
        return 1;
    else
    {
        char *ptr = strstr(*uri, "//");
        if (ptr)
        {
            ptr += 2;
            char *ptr2 = strstr(ptr, "/");
            *uri = ptr2;
            strncpy(host, ptr, ptr2 - ptr);
            host[ptr2 - ptr] = '\0';
            ptr = strstr(host, ":");
            if (ptr)
            {
                int portnum = atoi(ptr + 1);
                sprintf(port, "%d", portnum);
                *ptr = '\0';
            }
            else
            {
                if (strstr(head, "https"))
                    sprintf(port, "%d", 443);
                else
                    sprintf(port, "%d", 80);
            }
            return 0;
        }
        else
        {
            unix_error("parse_uri error");
            return -1;
        }
    }
}
/* $end parse_uri */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
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
    sprintf(buf, "<hr><em>The Proxy Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */
