/* 薛昊男 2200013194 */
#include "csapp.h"
#include "http.h"
#include "hash.h"
#include "cache.h"
#define DEBUG
#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#else
#define dbg_printf(...)
#endif

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define SEED 114514

/* 传递给线程的参数的结构, 原本以为传递给线程的参数可能比较多, 所以用了一个结构打包起来 */
/* 不过到最后发现只有一个参数, 也就没改 */
/* 和in_addr把一个IPv4地址放到一个结构里如出一辙地奇怪 */
typedef struct {
    int connfd;
} thread_args;

extern cache_object_t Cache[CACHE_SLOT];

extern unsigned int global_time;

/* 一个全局的读写锁, 用于控制对Cache和global_time的读写 */
/* 一开始想着global_time用一个原子类型, 编译器标准提到C11来着 */
/* 不过后来发现global_time的写和Cache的读写在一起, 可以一起用读写锁管了 */
/* 所以就没折腾原子类型 */
pthread_rwlock_t rwlock;

void *thread(void *vargp) {
    pthread_detach(pthread_self());
    thread_args *argp;
    size_t read_len, write_len, url_len, content_len;
    int facing_server_fd;
    unsigned int hash_result;
    HTTPRequest request;
    char *real_request;
    char *response_buf;
    char url[MAXLINE];
    cache_object_t *cobjp;
    
    /* 强转一下, 居然不让变量覆盖(shadow), 比Rust差远了 */
    argp = (thread_args *)vargp;
    
    request_init(&request);
    if(read_request(argp->connfd, &request) == -1) {
        fprintf(stderr, "Malformed Httprequest\n");
        display_request(&request);
    }
    printf("Thread[%p]: received request from client\n", pthread_self());
   
    
    
    /* 获取url */
    if ((url_len = get_url(&request, url, MAXLINE)) == -1) {
        fprintf(stderr, "No enough buffer\n");
    }
    /* 根据url做哈希 */
    hash_result = MurmurHash(url, url_len, SEED);

    /* 即将访问Cache, 读锁锁上 */
    pthread_rwlock_rdlock(&rwlock);
    /* 先尝试在本地缓存中按照哈希值寻找缓存项 */
    if((cobjp = find_id(Cache, CACHE_SLOT, hash_result)) != NULL) {
        /* 如果在缓存中找到了, 则直接取出来, 发送 */
        cobjp->timestamp = global_time++;
        /* 将全局变量复制到线程内部来, 然后尽快释放锁 */
        /* 主要是考虑到如果直接使用全局的资源去send_response到客户端 */
        /* 如果发送相应这个IO过程阻塞较长那么会使所有等待写锁的线程阻塞 */
        response_buf = malloc(sizeof(char) * MAX_OBJECT_SIZE * 2);
        memset(response_buf, 0, sizeof(char) * MAX_OBJECT_SIZE * 2);
        memcpy(response_buf, cobjp->objp, cobjp->obj_len);
        printf("Thread[%p]: received %ld bytes from cache\n", pthread_self(), cobjp->obj_len);
        /* 访问全局变量结束, 释放锁 */
        pthread_rwlock_unlock(&rwlock);
        send_response(argp->connfd, response_buf, sizeof(char) * MAX_OBJECT_SIZE * 2);
        /* 释放malloc的buffer, 防止内存泄漏 */
        free(response_buf);
        response_buf = NULL;
        /* 关闭套接字描述符 */
        close(argp->connfd);
        /* 释放参数结构 */
        free(argp);
        return NULL;
    }

    /* 没在缓存中找到, 先把读写锁释放掉 */
    pthread_rwlock_unlock(&rwlock);
    /* 没在缓存中找到, 再尝试构建请求向服务器发送 */
    facing_server_fd = open_clientfd(request.host, request.port);
    printf("Thread[%p]: facing_server_fd: %d\n", pthread_self(), facing_server_fd);
    if (facing_server_fd == -1) {
        /* 无法打开则直接清理线程并且返回 */
        close(argp->connfd);
        free(argp);
        return NULL;
    }
        
    real_request = malloc(sizeof(char) * MAXLINE);
    if ((write_len = construct_real_request(&request, real_request)) == -1) {
        fprintf(stderr, "Thread[%p]: Fail to construct real request\n", pthread_self());
    };
    
    /* 向服务器发送请求 */
    send_request(facing_server_fd, real_request, write_len);
    /* 释放请求发送阶段的资源 */
    free(real_request);
    real_request = NULL;
    request_clear(&request);

    /* 接受响应 */
    response_buf = malloc(sizeof(char) * MAXLINE);
    char *temp_guard = response_buf;
    if ((response_buf = read_response(facing_server_fd, response_buf, &read_len)) == (void *)-1) {
        fprintf(stderr, "Thread[%p]: Fail to read_response\n", pthread_self());
        /* 如果不能读取到响应, 那么直接清理并且退出本线程 */
        free(temp_guard);
        response_buf = NULL;
        close(argp->connfd);
        free(argp);
        return NULL;
    }

    /* 向客户端转发响应 */
    printf("Thread[%p]: received %ld bytes from server\n", pthread_self(), read_len);
    send_response(argp->connfd, response_buf, read_len);
    printf("Thread[%p]: response sent\n", pthread_self());
    /* 获取Content-Length大小, 看看是否可以缓存 */
    content_len = get_content_length(response_buf);
    
    if (content_len <= MAX_OBJECT_SIZE) {
        /* 如果允许cache那么就cache */
        cobjp = malloc(sizeof(cache_object_t));
        cobjp->id = hash_result;
        cobjp->objp = response_buf;
        cobjp->obj_len = read_len;
        pthread_rwlock_wrlock(&rwlock);
        cobjp->timestamp = global_time++;
        store_obj(Cache, CACHE_SLOT, cobjp);
        pthread_rwlock_unlock(&rwlock);
    } else {
        /* 否则才释放 */
        free(response_buf);
        response_buf = NULL;
    }
    /* 关闭套接字描述符 */
    close(argp->connfd);
    /* 释放参数结构 */
    free(argp);
    return NULL;
}

int main(int argc, char **argv)
{
    /* 忽略SIGPIPE信号 */
    signal(SIGPIPE, SIG_IGN);

    int listenfd, connfd;
    char client_hostname[MAXLINE], client_port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* 检查命令行参数 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    /* 初始化缓存 */
    init_cache(Cache, CACHE_SLOT);
    /* 初始化读写锁 */
    pthread_rwlock_init(&rwlock, NULL);
    /* 初始化时间戳 */
    global_time = 0;
    /* 在命令行指定的端口号上监听client的连接 */
    listenfd = open_listenfd(argv[1]);
    while(1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);

        /* 放到堆上, 使得线程可以从堆空间上读到量. 不可以放在主线程的栈空间上. */
        thread_args *arg = malloc(sizeof(thread_args));
        arg->connfd = connfd;
        pthread_create(&tid, NULL, thread, arg);
    }
    return 0;
}
