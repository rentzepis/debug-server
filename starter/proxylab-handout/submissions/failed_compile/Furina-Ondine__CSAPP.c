#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

typedef struct url_t{
    char host[MAXLINE];
    char port[6];
    char path[MAXLINE];
} url_t;

#define SBUF_SIZE 100

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

void doit(int fd);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
int parse_url(char *url, char *host, char *port, char *path);
int get_cpu_count();
void *thread(void *sbuf_ptr);

int main(int argc, char *argv[])
{
    //check command-line args
    if(argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
    }

    //预初始化各种资源
    cache_init();
    sbuf_t *sbuf = sbuf_init(SBUF_SIZE);
    const int cpu_count = get_cpu_count();
    for(int i=0;i<cpu_count<<2;i++){
        pthread_t tid;
        pthread_create(&tid, NULL, thread, sbuf);
        Pthread_detach(tid);
    }
    
    //开始监听事件循环
    int listenfd = Open_listenfd(argv[1]);

    while(1){
        struct sockaddr_storage client_con;
        socklen_t client_len = sizeof client_con;
        const int client_fd = Accept(listenfd, (SA*)&client_con, &client_len);
        sbuf_push(sbuf, client_fd);

        char host[MAXLINE], port[MAXLINE];
        Getnameinfo((SA*)&client_con, client_len, host, sizeof host, port, sizeof port, 0);
        printf("Accepted connection from (%s, %s)\n", host, port);
    }

    //回收资源
    sbuf_destory(sbuf);
    return 0;
}

void doit(const int client_fd){
    char buf[MAXLINE], method[16], url[MAXLINE], version[16];
    rio_t client_rio;
    Rio_readinitb(&client_rio, client_fd);

    //读取与解析请求头
    if(!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;
    sscanf(buf, "%s %s %s", method, url, version);
    if (strcasecmp(method, "GET")){
        clienterror(client_fd, method, "501", "Not Implemented", 
            "proxy does not implement this method");
        return;
    }

    //解析uri
    url_t url_info;
    if(parse_url(url, url_info.host, url_info.port, url_info.path)){
        clienterror(client_fd, "invaild url", "400", "Bad Request", 
            "parse url failed");
        return;
    }

    //查询缓存
    if(cache_query(url, client_fd))
        return;

    //连接服务器
    int server_fd = Open_clientfd(url_info.host, url_info.port);
    if(server_fd < 0){
        clienterror(client_fd, "network", "404", "connection refused", 
            "proxy cannot connect to host");
        return;
    }

    //转发给服务器
    sprintf(buf, "GET %s  HTTP/1.0\r\n%s%s%s\r\n", url_info.path, 
        user_agent_hdr, connection_hdr, proxy_connection_hdr);
    Rio_writen(server_fd, buf, strlen(buf));

    //添加缓存并转发
    cache_add(url, server_fd, client_fd);

    Close(server_fd);
    return;
}

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
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */

/*
 * parse_uri - parse URL into host and port and path args
 */
/* $begin parse_url */
int parse_url(char *url, char *restrict host, char *restrict port, char *restrict path) 
{
    const static int http_prefix_len = strlen("http://");
    if(strncasecmp(url, "http://", http_prefix_len)){
        return -1;
    }

    char *const host_ptr = url + http_prefix_len;
    char *const port_ptr = strchr(host_ptr, ':');
    char *const path_ptr = strchr(host_ptr, '/');

    if(port_ptr == NULL){
        strcpy(port, "80");
        *path_ptr = '\0';
        strncpy(host, host_ptr, MAXLINE);
        *path_ptr = '/';
        strncpy(path, path_ptr, MAXLINE);
    }else{
        *port_ptr = '\0';
        strncpy(host, host_ptr, MAXLINE);
        *port_ptr = ':';
        *path_ptr = '\0';
        strncpy(port, port_ptr+1, 6);
        *path_ptr = '/';
        strncpy(path, path_ptr, MAXLINE);
    }

    return 0;
}
/* $end parse_url */

/**
 * get_cpu_count - get cpu num
 */
/* $begin get_cpu_count */
int get_cpu_count(){
    #ifdef _WIN32
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        return sysinfo.dwNumberOfProcessors;
    
    #elif defined(__linux__)
        return sysconf(_SC_NPROCESSORS_ONLN);
    
    #elif defined(__APPLE__)
        int count;
        size_t size = sizeof(count);
        sysctlbyname("hw.ncpu", &count, &size, NULL, 0);
        return count;
    
    #else
        return 1; // 默认值
    #endif
}
/* $end get_cpu_count */

/**
 * 线程池处理事件循环
 */
/* $begin thread */
void *thread(void *sbuf_ptr){
    while(1){
        const int client_fd = sbuf_pop(sbuf_ptr);
        doit(client_fd);
        Close(client_fd);
    }
}
/* $end thread */
