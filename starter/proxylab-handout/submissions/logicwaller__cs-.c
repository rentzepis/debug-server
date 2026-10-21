/*
 * proxy.c - 在realpage的得分为8/20，实在不知道怎么优化
 */

#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* 辅助函数 */
void *thread(void *fd);
void process(int connfd);
size_t read_requesthdrs(rio_t *rp, char *req_header_buf);
size_t prase_response(rio_t *rp, char *content_buf);
int write_content(int fd, rio_t *rp, size_t content_length, char *object);
void prase_uri(char *uri, char *protocol, char *hostname, char *path, char *port);
void combine_request(char *request, char *method, char *path,
         char *version, char *req_header_buf, char *host);
void https_connection(rio_t *rio_client, rio_t *rio_server, int connfd, int serverfd);
void print_with_escape(const char *str);
int extract_and_delete(char *src, const char *dest, char *res);

int main(int argc, char **argv)
{
    /* 重新设置信号处理 */
    signal(SIGPIPE, SIG_IGN);

    /* 初始化cache */
    init_cache();

    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        // printf("Proxy: Waiting connection...\n");
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        // printf("Proxy: Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfd);                // 处理连接                                           
    }
    Close(listenfd); //关闭监听连接
    return 0;
}

/*
 * thread - 进程处理
 */
void *thread(void *fd){
    Pthread_detach(pthread_self()); //deatch化线程
    int connfd = *((int *)fd); //获得connfd
    free(fd); //释放fd内存

    process(connfd); // 处理函数
    
    close(connfd); //关闭客户端连接
    return NULL;
}

/*
 * process - 处理连接
 */
void process(int connfd){
    rio_t rio_client, rio_server;
    char method[MAXLINE], uri[MAX_OBJECT_SIZE], version[MAXLINE];
    char buf[MAX_OBJECT_SIZE], req_header_buf[MAXBUF], content_buf[MAXBUF];
    char protocol[MAXLINE], hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char request[MAXLINE], object[MAX_OBJECT_SIZE], postl[MAXLINE];
    char *ptr;
    int serverfd;
    size_t content_length, object_size, post_length;
    static char *connection_established = "HTTP/1.1 200 Connection Established\r\n\r\n";//https连接时的确认连接代码

    /* 读取并解析request与headers */
    rio_readinitb(&rio_client, connfd);
    if(rio_readlineb(&rio_client, buf, MAX_OBJECT_SIZE) <= 0){
        fprintf(stderr, "Falied to read request line, error: %s\n",strerror(errno));
        return;
    }
    // sio_puts(buf);
    sscanf(buf, "%s %s %s", method, uri, version);             //解析request
    if(strcmp(method, "GET") != 0){
        sio_puts("Proxy: not GET, returnning\n");
        close(connfd);
        return;
    }
    if (strlen(uri) > MAXLINE - 1) uri[MAXLINE - 1] = '\0'; // 当uri过长时，截取一部分即可
    strcpy(version, "HTTP/1.0");                            // 将version强制改为HTTP/1.0

    /* 检查是否已被cache，若是，则直接读取 */
    if (strcmp(method, "POST") != 0 && cache_find(uri, object, &object_size) == 1){ //在缓存中找到
        sio_puts("Proxy: Using Cache\n");
        if(rio_writen(connfd, object, object_size) != object_size); //写回客户端
        return;
    }
    // if(query_cache(&rio_client, uri)){
    //     return;
    // }
   
    /* 读取headers */
    if(read_requesthdrs(&rio_client, req_header_buf) > MAXBUF){
        sio_puts("Proxy ERROR: Too many headers!!!\n");
        sio_puts("returning\n");
        return;
    }          
    /* 解析url */
    prase_uri(uri, protocol, hostname, path, port);
    /* 合并request */
    combine_request(request, method, path, version, req_header_buf, hostname);
    /* 转发request */
    if((serverfd = open_clientfd(hostname, port)) < 0){
        sio_puts("Proxy: Unable to open server");
        sio_puts(hostname);
        sio_puts(port);
        sio_puts("\nreturning\n");
        return;
    }
    // sio_puts("Proxy: Sending request\n");
    // print_with_escape(request);
    rio_writen(serverfd, request, strlen(request));
    // sio_puts("Proxy: Request is sent\n");

    /* 若请求为POST，则还需读取客户端数据向服务器发送 */
    if(strcmp(method, "POST") == 0){
        /* 获取post_length */
        ptr = strstr(req_header_buf, "Content-Length");
        if(ptr != NULL) sscanf(ptr, "Content-Length: %s\r\n", postl);
        else unix_error("未匹配Content-Length\n");
        post_length = atol(postl);
        /* 读取并发送给服务器 */
        // sio_puts("Proxy: Posting content\n");
        if(write_content(serverfd, &rio_client, post_length, NULL) == -1){
            close(serverfd);
            return;
        }
        // sio_puts("Proxy: Content is posted\n");
    }

    if (strcmp(protocol, "http") == 0){//http连接
        /* 解析response的header并传回客户端 */
        rio_readinitb(&rio_server, serverfd);
        // sio_puts("Proxy: Prasing response header\n");
        content_length = prase_response(&rio_server, content_buf);
        // sio_puts("Proxy: Response header is prased, sending it to client\n");
        // print_with_escape(content_buf);
        rio_writen(connfd, content_buf, strlen(content_buf));//这里一定用strlen而不是sizeof!!!
                                                            //strlen是实际字符串长度，sizeof是所占内存总长度（这里永远是MAXLINE）
        if(strlen(content_buf) <= MAX_OBJECT_SIZE) strcpy(object, content_buf);                        //缓存记录headers
        // sio_puts("Proxy: Response header is sent\n");

        /* 将content传回客户端 */
        // printf("Proxy: Sending %ld bytes of body\n", content_length);
        object_size = strlen(content_buf) + content_length;
        if(strcmp(method, "POST") != 0 && object_size <= MAX_OBJECT_SIZE){ //缓存大小适合,插入
            if(write_content(connfd, &rio_server, content_length, object) == -1){
                close(serverfd);
                return;
            }
            cache_insert(uri, object, object_size); 
            // add_cache(uri, object, object_size);
        } else{ //缓存过大，不进行缓存
            if(write_content(connfd, &rio_server, content_length, NULL) == -1){
                close(serverfd);
                return;
            }
        }
    } else{//https连接
        // sio_puts("Proxy: Https connection establishing\n");
        /* 向客户端发送链接建立code */
        rio_writen(connfd, connection_established, strlen(connection_established));
        /* 客户端服务器同时读写信息 */
        rio_readinitb(&rio_server, serverfd);
        https_connection(&rio_client, &rio_server, connfd, serverfd);
    }
    /* 关闭服务器连接 */
    close(serverfd);
 
    // sio_puts("Proxy: Process done.\n\n");
    return;
}


/*
 * read_requesthdrs - read HTTP(s) request headers，返回读取的总长度
 */
size_t read_requesthdrs(rio_t *rp, char *req_header_buf){
    char buf[MAXLINE];
    ssize_t n;
    size_t total = 0;
    req_header_buf[0] = '\0';

    n = rio_readlineb(rp, buf, MAXLINE);
    total += n;
    // sio_puts(buf);
    strcat(req_header_buf, buf);
    while(strcmp(buf, "\r\n")){
        if((n = rio_readlineb(rp, buf, MAXLINE)) < 0) break;
        total += n;
        // sio_puts(buf);
        if(total > MAXBUF) return total;//防止过长请求
        strcat(req_header_buf, buf);
    }
    return total;
}

/*
 * prase_response - 解析response的header部分，返回值为content_length
 */
size_t prase_response(rio_t *rp, char *content_buf){
    char buf[MAXLINE], tem[MAXLINE];
    char *ptr;
    ssize_t n;
    content_buf[0] = '\0';

    /* 读取header */
    rio_readlineb(rp, buf, MAXLINE);
    sio_puts(buf);
    strcat(content_buf, buf);
    while(strcmp(buf, "\r\n")){
        if((n = rio_readlineb(rp, buf, MAXLINE)) < 0) break;
        sio_puts(buf);
        strcat(content_buf, buf);
    }

    /* 获取content_length */
    ptr = strstr(content_buf, "Content-length");
    if (ptr != NULL){
        sscanf(ptr, "Content-length: %s\r\n", tem);
    }else{
        ptr = strstr(content_buf, "Content-Length");
        if (ptr != NULL){
            sscanf(ptr, "Content-Length: %s\r\n", tem);
        } else{ //没有Content-length，返回0表示无写回
            return 0;
        }
    }
    
    return atol(tem);
}

/*
 * write_content - 将rp中的剩余内容传回fd中，总长度为content_length。返回值为1时，cache长度小于最大长度，可以被缓存
 */
int write_content(int fd, rio_t *rp, size_t content_length, char *object){
    size_t now, n;
    size_t left = content_length;
    size_t cache_len = 0;
    if(object != NULL) cache_len = strlen(object);
    char buf[MAXBUF];

    now = left > MAXBUF ? MAXBUF : left;
    while((n = rio_readnb(rp, buf, now))){
        if (n  < 0){
            sio_puts("Proxy ERROR: rio_readnb错误\n");
            return -1;
        }
        rio_writen(fd, buf, n);
        left -= n;
        
        if (object != NULL){
            memcpy(object + cache_len, buf, n);
            cache_len += n;
        }
        now = left > MAXBUF ? MAXBUF : left;
    }

    if (left != 0){
        sio_puts("Proxy Warning: content_length与实际写回量不符\n");
        return -1;
    }
    return 1;
}

/*
 * prase_uri - 给定uri，解析获得其protocol(http or https), hostname, path, port(未指定则为默认值80)
 */
void prase_uri(char *uri, char *protocol, char *hostname, char *path, char *port){
    char *ptr, rest[MAXLINE];

    /* 获得protocol部分 */
    ptr = strstr(uri, "http");
    if (ptr != NULL){// 若uri里有http(s)
        ptr = index(uri, ':');
        if (!ptr) unix_error("Invalid URI: no ':' found");
        strncpy(protocol, uri, ptr - uri);   //获取http / https
        protocol[ptr-uri] = '\0';            //正确处理字符串结尾
        strcpy(rest, ptr + 3);              //去除"http(s)://"
    }else{// 否则，为https连接，进行处理
        strcpy(protocol, "https");
        strcpy(rest, uri);
    }

    /* 获得path部分 */
    ptr = index(rest, '/');
    if(ptr != NULL){
        strcpy(path, ptr);
        *ptr = '\0';                    //去除path的部分
    }else strcpy(path, "");             //若不存在'/'，则path为空值即可 
    
    /* 获得hostname和port部分 */
    ptr = index(rest, ':');
    if (ptr != NULL){                   //若存在port
        strncpy(hostname, rest, ptr - rest);//获取hostname
        strcpy(rest, ptr + 1);          //去除hostname
        strcpy(port, rest);             //剩下的都为port
    }else{                              //若不存在port
        strcpy(port, "80");             //port默认为80         
        strcpy(hostname, rest);        //剩下的都为hostname
    }
}

/*
 * combine_request - 将请求集合起来为一条字符串request，每一行用\r\n分割
 */
void combine_request(char *request, char *method, char *path,
         char *version, char *req_header_buf, char *host)
{
    char tem[MAXLINE], rest[MAXBUF];
    request[0] = '\0';

    // 第一行 <method> <path> <version>
    sprintf(tem, "%s %s %s\r\n", method, path, version);
    strcpy(request, tem);
    memset(tem, 0, sizeof(tem));        //清空tem

    // Host行
    strcpy(rest, req_header_buf);       //存储headers

    if (extract_and_delete(rest, "Host", tem) == 0){    //若原请求不存在Host，则添加
        sprintf(tem, "Host: %s\r\n", host);
    }
    strcat(request, tem);               //将tem写入最终的request中
    memset(tem, 0, sizeof(tem));        //清空tem

    // User-Agent行
    extract_and_delete(rest, "User-Agent", tem);//若存在，只需要删除那一行即可，无需结果
    memset(tem, 0, sizeof(tem));        //清空tem

    strcat(request, user_agent_hdr);    //将User-Agent写入最终的request中

    // Proxy-Connection行
    extract_and_delete(rest, "Proxy-Connection", tem);//若存在，只需要删除那一行即可，无需结果
    memset(tem, 0, sizeof(tem));        //清空tem

    strcat(request, "Proxy-Connection: close\r\n");    //将Prox-Connection写入最终的request中

    // Connection行
    extract_and_delete(rest, "Connection", tem);//若存在，只需要删除那一行即可，无需结果
    memset(tem, 0, sizeof(tem));        //清空tem

    strcat(request, "Connection: close\r\n");    //将Connection写入最终的request中

    //其余行(最后一行自带多出来的"\r\n"，无需再添加)
    strcat(request, rest);
}

/*
 * extract_and_delete - 给定src,提取"<dest>: .../r/n"的部分存入res中,并在src中删除该部分
 *                      若src里存在dest，返回1；否则什么都不做，返回0
 *                      (获取request对应部分用)
 *                      注：res必须为空字符串
 */
int extract_and_delete(char *src, const char *dest, char *res){
    char *ptr, *ptr2;

    ptr = strstr(src, dest);
    if (ptr != NULL){                   //若src有dest
        ptr2 = strstr(ptr, "\r\n");
        strncpy(res, ptr, (ptr2+2) - ptr);
        memmove(ptr, ptr2 + 2, strlen(ptr2 + 2) + 1);     //删除提取到的部分
        return 1;
    }else{                              //src没有dest
        return 0;
    }
}

/*
 * https_connection - 在https情况下对服务器和客户端进行同时读写
 */
// void https_connection(rio_t *rio_client, rio_t *rio_server, int connfd, int serverfd){
//     char buf[MAXBUF];
//     ssize_t n;

//     while (1) {
//         /* server -> client */
//         if((n = rio_readnb(rio_server, buf, MAXBUF)) <= 0) break;
//         rio_writen(connfd, buf, n);

//         /* client -> server */
//         if((n = rio_readnb(rio_client, buf, MAXBUF)) <= 0) break;
//         rio_writen(serverfd, buf, n);
//     }
// }
void https_connection(rio_t *rio_client, rio_t *rio_server, int connfd, int serverfd) {
    fd_set readfds;
    char buf[MAXBUF];
    
    while (1) {
        FD_ZERO(&readfds);
        FD_SET(connfd, &readfds);
        FD_SET(serverfd, &readfds);
        
        int maxfd = connfd > serverfd ? connfd : serverfd;
        if (select(maxfd + 1, &readfds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) continue;
            break;
        }
        
        // 客户端 -> 服务器
        if (FD_ISSET(connfd, &readfds)) {
            ssize_t n = rio_readnb(rio_client, buf, MAXBUF);
            if (n <= 0) break;
            if (rio_writen(serverfd, buf, n) != n) break;
        }
        
        // 服务器 -> 客户端
        if (FD_ISSET(serverfd, &readfds)) {
            ssize_t n = rio_readnb(rio_server, buf, MAXBUF);
            if (n <= 0) break;
            if (rio_writen(connfd, buf, n) != n) break;
        }
    }
}

/*
 * print_with_escape - 输出字符串包括转义符（调试用）
 */
void print_with_escape(const char *str) {
    char tem[2];
    tem[1] = '\0';
    while (*str) {
        if (*str == '\n') {
            sio_puts("\\n");
        } else if (*str == '\r') {
            sio_puts("\\r");
        } else if (*str == '\t') {
            sio_puts("\\t");
        } else {
            tem[0] = *str;
            sio_puts(tem);
        }
        str++;
    }
    sio_puts("\n");
}