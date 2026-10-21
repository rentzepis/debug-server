#include <stdio.h>
#include <stdlib.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct URI {
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
} Uri;

void doit(int fd);
void parse_uri(char *uri, Uri* uri_data);
void build_header(char *server_buf, Uri* uri_data, rio_t *client_rio);

/* 处理SIGPIPE信号 */
void sigpipe_handler(int sig)
{
    printf("sigpipe_handler");
    return;
}

int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, sigpipe_handler);
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s)\n", hostname, port);
        doit(connfd);
        Close(connfd);
    }
    printf("%s", user_agent_hdr);
    return 0;
}

/**
 * 与 tint web 不同的是转发客户端请求到服务器，并接收服务器的回复，返还给客户端
*/
void doit(int clientfd) { 
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char client_buf[MAXLINE], server_buf[MAXLINE];

    rio_t client_rio, server_rio;
    int serverfd;

    Rio_readinitb(&client_rio, clientfd);
    Rio_readlineb(&client_rio, client_buf, MAXLINE);
    sscanf(client_buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        fprintf(stderr, "Proxy does not implement the method %s\n", method);
        return;
    }

    Uri *uri_data = (Uri *)malloc(sizeof(Uri)); // parse 后的 uri

    parse_uri(uri, uri_data);

    // 设置好给服务器转发的 header
    build_header(server_buf, uri_data, &client_rio);

    // 连接服务器并转发
    serverfd = Open_clientfd(uri_data->host, uri_data->port);
    if (serverfd < 0) {
        fprintf(stderr, "Proxy connect server fail\n");
        return;
    }

    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, server_buf, strlen(server_buf));

    // 回复客户端
    size_t n;
    while ((n = Rio_readlineb(&server_rio, client_buf, MAXLINE)) != 0) {
        printf("Proxy receive %d bytes, then send back to client\n", (int)n);
        Rio_writen(clientfd, client_buf, n);
    }
    Close(serverfd);
}

void parse_uri(char *uri, Uri* uri_data) {
    printf("\nparsing uri: %s\n\n", uri);

    // 忽略开头的协议名
    char *ptr = strstr(uri, "//");
    if (ptr == NULL) ptr = uri;
    else ptr += 2;
    uri = ptr;

    char *port_ptr = strstr(uri, ":");
    if (port_ptr == NULL) {
        port_ptr = strstr(uri, "/");
        sscanf(port_ptr, "%s", uri_data->path);
        strcpy(uri_data->port, "80");
    } else {
        int tmp;
        sscanf(port_ptr+1, "%d%s", &tmp, uri_data->path);
        sprintf(uri_data->port, "%d", tmp);
    }
    *port_ptr = '\0';
    strcpy(uri_data->host, uri);
    printf("parse result:\nhost:%s\nport:%s\npath:%s\n\n", uri_data->host, uri_data->port, uri_data->path);
}

void build_header(char *server_buf, Uri* uri_data, rio_t *client_rio) {
    // writeup 中写的默认需要的 header
    char *User_Agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    char *conn_hdr = "Connection: close\r\n";
    char *prox_hdr = "Proxy-Connection: close\r\n";
    char *host_hdr_format = "Host: %s\r\n";
    char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
    char *endof_hdr = "\r\n";

    char buf[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE], other_hdr[MAXLINE];
    // writeup 里要求转发给服务器中的 request(即第一行) 只留 path
    sprintf(request_hdr, requestlint_hdr_format, uri_data->path);
    while(Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, endof_hdr)) break;
        // 如果有 host 就直接用
        if (!strncasecmp(buf, "Host", strlen("Host"))) {
            strcpy(host_hdr, buf);
            continue;
        }
        // 如果是其他 headers
        if (strncasecmp(buf, "Connection", strlen("Connection")) &&
            strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) &&
            strncasecmp(buf, "User-Agent", strlen("User-Agent"))) {
                strcat(other_hdr, buf);
        } 
    }
    if (strlen(host_hdr) == 0)
        sprintf(host_hdr, host_hdr_format, uri_data->host);
    
    sprintf(server_buf, "%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            User_Agent,
            conn_hdr,
            prox_hdr,
            other_hdr,
            endof_hdr);
}