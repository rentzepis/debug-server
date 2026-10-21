#include "csapp.h"
#include "sembuf.h"
#include "cache.h"

#define MAX_HOSTNAME_LEN 255
#define MAX_PATH_LEN 2048

#define NTHREADS 8
#define SEMBUF_SIZE 16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *http_version = "HTTP/1.0";
static char *http_port = "80";

void *transaction_thread(void *vargp);
void transaction(int fdFromClient);
int parse_url(const char *url, char *hostname, char *relative_path, char *port);
int is_valid_hostname(const char *hostname);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

sembuf_t sembuf_fdFromClient; /* shard buffer of connected fd */
lru_cache_t web_lru_cache;    /* web cache*/
extern rwLock_t rwLock_global;/* cache's read write lock */

int main(int argc, char **argv)
{
    int listenfd, fdFromClient;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    sembuf_init(&sembuf_fdFromClient, SEMBUF_SIZE);
    LRUCache_init(&web_lru_cache);
    rwLock_init(&rwLock_global);
    for (int i = 0; i < NTHREADS; i++){
        Pthread_create(&tid, NULL, transaction_thread, NULL);
    }

    while (1)
    {
        clientlen = sizeof(clientaddr);
        fdFromClient = Accept(listenfd, (SA *)&clientaddr, &clientlen);   // accept from user client
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sembuf_insert(&sembuf_fdFromClient, fdFromClient);
    }
}

void *transaction_thread(void *vargp){
    Pthread_detach(pthread_self());
    while (1){
        int fdFromClient = sembuf_remove(&sembuf_fdFromClient);
        transaction(fdFromClient);
        Close(fdFromClient);
    }
}

void transaction(int fdFromClient){
    char buf[MAXLINE], method[MAXLINE], fullURL[MAXLINE], version[MAXLINE];
    char serverHostName[MAX_HOSTNAME_LEN], serverURI[MAX_PATH_LEN], serverPort[32];
    int fdToServer;
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fdFromClient);
    if (!Rio_readlineb(&rio, buf, MAXLINE))
        return;
    // printf("%s", buf);
    sscanf(buf, "%s %s %s", method, fullURL, version);

    if (strcmp(method, "GET")){
        clienterror(fdFromClient, method, "501", "Not Implemented",
            "Proxy does not implement this method");
        return;
    }
    cache_node_t *cacheObject = LRUCache_get(&web_lru_cache, fullURL);
    if (cacheObject != NULL){
        // cacheObject转发给client
        Rio_writen(fdFromClient, cacheObject->value, cacheObject->valSize);
        return;
    }

    // uri提取hostname
    parse_url(fullURL, serverHostName, serverURI, serverPort);

    // 和host建立连接
    if ((fdToServer = Open_clientfd(serverHostName, serverPort)) < 0){
        clienterror(fdFromClient, method, "502", "Bad Gateway", "Proxy gets invalid hostname");
        return;
    }

    // 向host转发请求
    sprintf(buf, "%s %s %s\r\n", method, serverURI, http_version);
    strcat(buf, "Host: ");
    strcat(buf, serverHostName);
    strcat(buf, "\r\n");
    strcat(buf, user_agent_hdr);
    strcat(buf, connection_hdr);
    strcat(buf, proxy_connection_hdr);
    strcat(buf, "\r\n");
    Rio_writen(fdToServer, buf, strlen(buf));
    // printf("forward request: %s", buf);

    // 接收host的response
    Rio_readinitb(&rio, fdToServer);
    
    int bytesRead;
    int totalBytesRead;
    
    char webObjectBuf[MAX_OBJECT_SIZE];
    while ((bytesRead = Rio_readnb(&rio, buf, MAXLINE))){
        Rio_writen(fdFromClient, buf, bytesRead);
        totalBytesRead += bytesRead;
        if (totalBytesRead <= MAX_OBJECT_SIZE){
            memcpy(&webObjectBuf[totalBytesRead - bytesRead], buf, bytesRead);
        }
    }
    if (totalBytesRead <= MAX_OBJECT_SIZE){
        LRUCache_put(&web_lru_cache, fullURL, webObjectBuf, totalBytesRead);
    }
   
    Close(fdToServer);
}

// 检查主机名是否合法
int is_valid_hostname(const char *hostname) {
    if (hostname == NULL || strlen(hostname) == 0 || strlen(hostname) > MAX_HOSTNAME_LEN) {
        return 0; // 长度非法
    }
    const char *start = hostname;
    const char *end = hostname + strlen(hostname);

    // 分割每个部分
    const char *segment_start = start;
    while (segment_start < end) {
        const char *segment_end = strchr(segment_start, '.');
        if (!segment_end) {
            segment_end = end; // 最后一段
        }

        // 检查每段长度
        size_t segment_length = segment_end - segment_start;
        if (segment_length == 0 || segment_length > 63) {
            return 0; // 每段长度非法
        }

        // 检查每段字符是否合法
        if (segment_start[0] == '-' || segment_start[segment_length - 1] == '-') {
            return 0; // 不允许以 '-' 开头或结尾
        }
        for (const char *p = segment_start; p < segment_end; p++) {
            if (!isalnum(*p) && *p != '-') {
                return 0; // 非法字符
            }
        }

        // 移动到下一段
        segment_start = segment_end + 1;
    }
    return 1; // 合法主机名
}

// 从full url path中提取hostname uri和端口
int parse_url(const char *url, char *hostname, char *relative_path, char *port) {
    const char *http_prefix = "http://";
    const char *https_prefix = "https://";
    const char *start;
    char *path_start;
    char *port_start;

    // 检查协议并跳过
    if (strncmp(url, http_prefix, strlen(http_prefix)) == 0) {
        start = url + strlen(http_prefix); // 跳过 "http://"
    } else if (strncmp(url, https_prefix, strlen(https_prefix)) == 0) {
        start = url + strlen(https_prefix); // 跳过 "https://"
    } else {
        start = url; // 无协议前缀
    }
    strcpy(port, http_port);   

    // 查找路径开始的位置
    path_start = strchr(start, '/');
    if (path_start) {
        // 查找端口号开始的位置
        port_start = strchr(start, ':');
        if (port_start && port_start < path_start) {
            // 主机名到端口号之间的部分
            size_t host_len = port_start - start;
            if (host_len > MAX_HOSTNAME_LEN) {
                return -1; // 主机名过长
            }
            strncpy(hostname, start, host_len);
            hostname[host_len] = '\0'; // 添加字符串结束符

            // 提取端口号
            int portLen = path_start - (port_start + 1);
            strncpy(port, port_start + 1, portLen);
            port[portLen] = '\0';
        } else {
            // 没有端口号
            size_t host_len = path_start - start;
            if (host_len > MAX_HOSTNAME_LEN) {
                return -1; // 主机名过长
            }
            strncpy(hostname, start, host_len);
            hostname[host_len] = '\0';
        }

        // 路径是从 path_start 开始的部分
        strcpy(relative_path, path_start);
    } else {
        // 如果没有路径，检查是否包含端口号
        port_start = strchr(start, ':');
        if (port_start) {
            size_t host_len = port_start - start;
            if (host_len > MAX_HOSTNAME_LEN) {
                return -1; // 主机名过长
            }
            strncpy(hostname, start, host_len);
            hostname[host_len] = '\0';

            // 提取端口号
            int portLen = path_start - (port_start + 1);
            strncpy(port, port_start + 1, portLen);
            port[portLen] = '\0';
        } else {
            // 没有路径也没有端口号
            strcpy(hostname, start);
        }
        strcpy(relative_path, "/");
    }

    // 检查主机名是否合法
    if (!is_valid_hostname(hostname)) {
        return -1; // 主机名非法
    }
    return 0; // 成功解析
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
    sprintf(buf, "<html><title>Request Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Proxy</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */