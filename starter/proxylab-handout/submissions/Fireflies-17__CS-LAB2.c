#include <stdio.h>
#include "csapp.h"

/* 缓存大小限制 */
#define MAX_CACHE_SIZE 1049000      // 最大缓存1MB
#define MAX_OBJECT_SIZE 102400      // 单个对象最大100KB

/* 固定的User-Agent头部 */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* 缓存块结构 */
typedef struct cache_block {
    char uri[MAXLINE];              // 请求的URI
    char *data;                     // 缓存的数据
    size_t size;                    // 数据大小
    struct cache_block *prev;       // 链表前驱（用于LRU）
    struct cache_block *next;       // 链表后继（用于LRU）
} cache_block;

/* 缓存结构 */
typedef struct {
    cache_block *head;              // LRU链表头（最近使用）
    cache_block *tail;              // LRU链表尾（最久未使用）
    size_t total_size;              // 当前缓存总大小
    pthread_rwlock_t lock;          // 读写锁
} cache_t;

/* 全局缓存 */
cache_t cache;


/* 函数声明 */
void doit(int fd);
void *thread(void *vargp);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void forward_request(int serverfd, char *method, char *path, char *hostname, rio_t *client_rio);
int forward_response_with_cache(int clientfd, int serverfd, char *uri);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* 缓存函数声明 */
void cache_init();
cache_block* cache_find(char *uri);
void cache_insert(char *uri, char *data, size_t size);
void cache_evict();
void cache_move_to_front(cache_block *block);


/*
 * main - 代理服务器主函数
 * 创建监听套接字，接受客户端连接，为每个连接创建独立线程
 */
int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* 检查命令行参数 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* 忽略SIGPIPE信号，防止写入已关闭的套接字时程序崩溃 */
    Signal(SIGPIPE, SIG_IGN);
    
    /* 初始化缓存 */
    cache_init();
    
    /* 在指定端口创建监听套接字 */
    listenfd = Open_listenfd(argv[1]);
    
    /* 主循环：持续接受客户端连接 */
    while (1) {
        clientlen = sizeof(clientaddr);
        
        /* 在堆上分配连接描述符，避免线程间的竞争条件 */
        connfdp = Malloc(sizeof(int));
        
        /* 接受客户端连接 */
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        /* 获取并打印客户端信息 */
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        
        /* 创建新线程处理该连接，实现并发 */
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    
    return 0;
}

/*
 * thread - 线程例程
 * 每个线程独立处理一个客户端连接
 * vargp指向连接描述符的指针
 */
void *thread(void *vargp)
{
    /* 从参数中提取连接描述符 */
    int connfd = *((int *)vargp);
    
    /* 分离线程，使其结束时自动释放资源 */
    Pthread_detach(Pthread_self());
    
    /* 释放堆上分配的连接描述符内存 */
    Free(vargp);
    
    /* 处理客户端请求 */
    doit(connfd);
    
    /* 关闭客户端连接 */
    Close(connfd);
    
    return NULL;
}

/*
 * doit - 处理一个HTTP请求/响应事务
 * 读取客户端请求，检查缓存，如未命中则连接服务器
 */
void doit(int clientfd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    rio_t client_rio;
    int serverfd;
    cache_block *block;

    /* 初始化RIO读缓冲区 */
    Rio_readinitb(&client_rio, clientfd);
    
    /* 读取请求行 */
    if (!Rio_readlineb(&client_rio, buf, MAXLINE)) {
        return;  // 读取失败，直接返回
    }
    
    printf("Request: %s", buf);
    
    /* 解析请求行，提取方法、URI和版本 */
    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        clienterror(clientfd, method, "400", "Bad Request", 
                    "Proxy could not parse the request");
        return;
    }
    
    /* 只支持GET方法 */
    if (strcasecmp(method, "GET")) {
        clienterror(clientfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }
    
    /* 检查缓存 */
    pthread_rwlock_rdlock(&cache.lock);
    block = cache_find(uri);
    if (block != NULL) {
        /* 缓存命中 */
        printf("Cache hit: %s\n", uri);
        rio_writen(clientfd, block->data, block->size);
        cache_move_to_front(block);
        pthread_rwlock_unlock(&cache.lock);
        return;
    }
    pthread_rwlock_unlock(&cache.lock);
    
    printf("Cache miss: %s\n", uri);
    
    /* 解析URI，提取主机名、端口号和路径 */
    parse_uri(uri, hostname, port, path);
    
    /* 连接到目标服务器 */
    serverfd = open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(clientfd, hostname, "500", "Internal Server Error",
                    "Proxy failed to connect to the server");
        return;
    }
    
    /* 向服务器转发请求 */
    forward_request(serverfd, method, path, hostname, &client_rio);
    
    /* 将服务器响应转发给客户端，并缓存 */
    forward_response_with_cache(clientfd, serverfd, uri);
    
    /* 关闭与服务器的连接 */
    Close(serverfd);
}

/*
 * parse_uri - 解析URI，提取主机名、端口和路径
 */
void parse_uri(char *uri, char *hostname, char *port, char *path)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;
    
    /* 查找"://"，定位主机名的起始位置 */
    hostbegin = strstr(uri, "//");
    if (hostbegin == NULL) {
        hostbegin = uri;  // 没有"://"，直接从uri开始
    } else {
        hostbegin += 2;   // 跳过"//"
    }
    
    /* 查找'/'，定位路径的起始位置 */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        strcpy(path, "/");  // 没有路径，使用默认根路径
        pathbegin = hostbegin + strlen(hostbegin);
    } else {
        strcpy(path, pathbegin);  // 复制路径部分
    }
    
    /* 查找':'，检查是否指定了端口号 */
    hostend = strchr(hostbegin, ':');
    if (hostend != NULL && hostend < pathbegin) {
        /* 端口号已指定 */
        len = hostend - hostbegin;
        strncpy(hostname, hostbegin, len);
        hostname[len] = '\0';
        
        /* 提取端口号 */
        len = pathbegin - hostend - 1;
        strncpy(port, hostend + 1, len);
        port[len] = '\0';
    } else {
        /* 端口号未指定，使用默认HTTP端口80 */
        len = pathbegin - hostbegin;
        strncpy(hostname, hostbegin, len);
        hostname[len] = '\0';
        strcpy(port, "80");
    }
}

/*
 * forward_request - 转发HTTP请求到服务器
 * 
 * 1. 发送请求行（转换为HTTP/1.0）
 * 2. 读取并转发客户端的请求头
 * 3. 添加/替换必需的请求头（Host, User-Agent, Connection, Proxy-Connection）
 */
void forward_request(int serverfd, char *method, char *path, char *hostname, rio_t *client_rio)
{
    char buf[MAXLINE];
    int has_host = 0, has_user_agent = 0, has_connection = 0, has_proxy_connection = 0;
    
    /* 发送请求行：将HTTP/1.1转换为HTTP/1.0 */
    sprintf(buf, "%s %s HTTP/1.0\r\n", method, path);
    Rio_writen(serverfd, buf, strlen(buf));
    
    /* 读取并处理客户端发送的请求头 */
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        /* 遇到空行，表示请求头结束 */
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
        
        /* 检查并处理特殊的请求头 */
        if (strncasecmp(buf, "Host:", 5) == 0) {
            has_host = 1;
            Rio_writen(serverfd, buf, strlen(buf));  // 转发原有的Host头
        } else if (strncasecmp(buf, "User-Agent:", 11) == 0) {
            has_user_agent = 1;
            Rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));  // 替换为固定的User-Agent
        } else if (strncasecmp(buf, "Connection:", 11) == 0) {
            has_connection = 1;  // 记录已有，稍后替换
        } else if (strncasecmp(buf, "Proxy-Connection:", 17) == 0) {
            has_proxy_connection = 1;  // 记录已有，稍后替换
        } else {
            Rio_writen(serverfd, buf, strlen(buf));  // 转发其他头部
        }
    }
    
    /* 确保必需的请求头存在 */
    
    /* Host头：指定目标服务器主机名 */
    if (!has_host) {
        sprintf(buf, "Host: %s\r\n", hostname);
        Rio_writen(serverfd, buf, strlen(buf));
    }
    
    /* User-Agent头：标识客户端类型 */
    if (!has_user_agent) {
        Rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));
    }
    
    /* Connection头：告知服务器请求后关闭连接 */
    if (!has_connection) {
        sprintf(buf, "Connection: close\r\n");
        Rio_writen(serverfd, buf, strlen(buf));
    }
    
    /* Proxy-Connection头：代理特有的连接控制 */
    if (!has_proxy_connection) {
        sprintf(buf, "Proxy-Connection: close\r\n");
        Rio_writen(serverfd, buf, strlen(buf));
    }
    
    /* 发送空行，标志请求头结束 */
    sprintf(buf, "\r\n");
    Rio_writen(serverfd, buf, strlen(buf));
}

/*
 * forward_response_with_cache - 转发服务器响应到客户端，并缓存
 * 从服务器读取所有数据，转发给客户端，如果大小合适则缓存
 */
int forward_response_with_cache(int clientfd, int serverfd, char *uri)
{
    char *object_buf = Malloc(MAX_OBJECT_SIZE);
    char buf[MAXLINE];
    ssize_t n;
    size_t total_size = 0;
    rio_t server_rio;
    int can_cache = 1;
    
    /* 初始化RIO读缓冲区 */
    Rio_readinitb(&server_rio, serverfd);
    
    /* 持续读取服务器数据并转发给客户端 */
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        /* 转发给客户端 */
        if (rio_writen(clientfd, buf, n) != n) {
            fprintf(stderr, "Error writing to client\n");
            Free(object_buf);
            return -1;
        }
        
        /* 如果可以缓存，累积数据 */
        if (can_cache && (total_size + n <= MAX_OBJECT_SIZE)) {
            memcpy(object_buf + total_size, buf, n);
            total_size += n;
        } else {
            can_cache = 0;  // 对象太大，无法缓存
        }
    }
    
    /* 检查读取是否出错 */
    if (n < 0) {
        fprintf(stderr, "Error reading from server\n");
        Free(object_buf);
        return -1;
    }
    
    /* 如果可以缓存，插入到缓存中 */
    if (can_cache && total_size > 0 && total_size <= MAX_OBJECT_SIZE) {
        cache_insert(uri, object_buf, total_size);
    } else {
        Free(object_buf);
    }
    
    return 0;
}

/*
 * clienterror - 向客户端返回错误信息
 * 构造并发送HTTP错误响应
 *   fd - 客户端连接描述符
 *   cause - 引起错误的原因（如方法名、文件名等）
 *   errnum - 错误代码（如"404"）
 *   shortmsg - 简短错误信息（如"Not Found"）
 *   longmsg - 详细错误描述
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];
    
    /* 构造HTTP响应主体（HTML格式） */
    strcpy(body, "<html><title>Proxy Error</title>");
    strcat(body, "<body bgcolor=\"ffffff\">\r\n");
    strcat(body, errnum);
    strcat(body, ": ");
    strcat(body, shortmsg);
    strcat(body, "\r\n");
    strcat(body, "<p>");
    strcat(body, longmsg);
    strcat(body, ": ");
    strcat(body, cause);
    strcat(body, "\r\n");
    strcat(body, "<hr><em>The Proxy server</em>\r\n");
    
    /* 发送HTTP响应头 */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    
    /* 发送HTTP响应主体 */
    Rio_writen(fd, body, strlen(body));
}


/*
 * cache_init - 初始化缓存
 */
void cache_init()
{
    cache.head = NULL;
    cache.tail = NULL;
    cache.total_size = 0;
    pthread_rwlock_init(&cache.lock, NULL);
}

/*
 * cache_find - 在缓存中查找URI
 * 返回：找到返回cache_block指针，否则返回NULL
 */
cache_block* cache_find(char *uri)
{
    cache_block *block = cache.head;
    
    while (block != NULL) {
        if (strcmp(block->uri, uri) == 0) {
            return block;
        }
        block = block->next;
    }
    
    return NULL;
}

/*
 * cache_insert - 插入新对象到缓存
 * 如果缓存满，先驱逐旧对象
 */
void cache_insert(char *uri, char *data, size_t size)
{
    cache_block *block;
    
    pthread_rwlock_wrlock(&cache.lock);
    
    /* 检查是否已经在缓存中 */
    if (cache_find(uri) != NULL) {
        pthread_rwlock_unlock(&cache.lock);
        Free(data);
        return;
    }
    
    /* 如果缓存空间不足，驱逐旧对象 */
    while (cache.total_size + size > MAX_CACHE_SIZE && cache.tail != NULL) {
        cache_evict();
    }
    
    /* 如果对象仍然太大，放弃缓存 */
    if (size > MAX_CACHE_SIZE) {
        pthread_rwlock_unlock(&cache.lock);
        Free(data);
        return;
    }
    
    /* 创建新缓存块 */
    block = Malloc(sizeof(cache_block));
    strcpy(block->uri, uri);
    block->data = data;
    block->size = size;
    block->prev = NULL;
    block->next = cache.head;
    
    /* 插入到链表头部（最近使用） */
    if (cache.head != NULL) {
        cache.head->prev = block;
    }
    cache.head = block;
    
    if (cache.tail == NULL) {
        cache.tail = block;
    }
    
    cache.total_size += size;
    
    pthread_rwlock_unlock(&cache.lock);
}

/*
 * cache_evict - 驱逐最久未使用的对象
 */
void cache_evict()
{
    cache_block *block = cache.tail;
    
    if (block == NULL) {
        return;
    }
    
    /* 从链表中移除 */
    if (block->prev != NULL) {
        block->prev->next = NULL;
    } else {
        cache.head = NULL;
    }
    
    cache.tail = block->prev;
    cache.total_size -= block->size;
    
    /* 释放内存 */
    Free(block->data);
    Free(block);
}

/*
 * cache_move_to_front - 将块移动到链表头部
 */
void cache_move_to_front(cache_block *block)
{
    if (block == cache.head) {
        return;  // 已经在头部
    }
    
    pthread_rwlock_wrlock(&cache.lock);
    
    /* 从当前位置移除 */
    if (block->prev != NULL) {
        block->prev->next = block->next;
    }
    if (block->next != NULL) {
        block->next->prev = block->prev;
    }
    if (block == cache.tail) {
        cache.tail = block->prev;
    }
    
    /* 插入到头部 */
    block->prev = NULL;
    block->next = cache.head;
    if (cache.head != NULL) {
        cache.head->prev = block;
    }
    cache.head = block;
    
    pthread_rwlock_unlock(&cache.lock);
}
