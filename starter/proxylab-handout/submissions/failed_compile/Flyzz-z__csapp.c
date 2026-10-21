#include "csapp.h"
#include "tiny/csapp.h"
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>

/* Recommended max cache and object sizes */
#define BUCKET 5087
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *version_line = "HTTP/1.0";

void* handle_client(void* cli_fd);


struct node
{
    char *key;
    size_t hash;
    char *data;
    size_t len;
    struct node *prev;
    struct node *next;
};

struct cache
{
    struct node head;
    struct node tail;
    size_t size;
    size_t cap;
};

void add_node(struct node *head,struct node *nd)
{
    struct node *next = head->next;
    nd->next = next;
    next->prev = nd;
    head->next = nd;
    nd->prev = head;
       
}

void del_node(struct node *nd,int fr)
{
    struct node *prev = nd->prev;
    struct node *next = nd->next;
    if(!prev)
        return;
    prev->next = next;
    next->prev = prev;
    if(fr)
    {
        free(nd->key);
        free(nd->data);
        free(nd);
    }

}

void put_cache(struct cache *cache,struct node *nd)
{
    if(nd->len>cache->cap)
        return;

    if(cache->size+nd->len<cache->cap)
    {
        add_node(&cache->head, nd);
    } else {
        struct node *p = cache->tail.prev,*pprev = NULL;
        while (p!=&cache->head&&cache->size+nd->len>cache->cap) {
            cache->size -= p->len;
            pprev = p->prev;
            del_node(p, 1);
            p = pprev;
        }
        add_node(&cache->head, nd);
    }
}

/*
    优先通过hash对比，再确认key
*/
struct node* find_cache(struct cache* cache,char *key,size_t hash)
{
    struct node* nd = cache->head.next;
    while (nd!=&cache->tail) {
        if(nd->hash==hash)
        {
            if(!strcmp(key, nd->key))
            {
                del_node(nd,0);
                add_node(&cache->head, nd);
                return nd;
            }
        }
        nd = nd->next;
    }
    return NULL;
}

size_t str_hash(char *str)
{
    size_t hash = 0;
    int len = strlen(str);
    while (len--) {
        hash  = (hash<<5) + *str++;
    }
    return hash;
}

struct out_buf
{
    char *out;
    size_t off;
};

struct handle_cli_param
{
    int cli_fd;
    struct cache *cache;
    pthread_rwlock_t *rw_lock;
};

void write_buf(struct out_buf* buf,char *val,size_t size)
{   
    strncpy(buf->out+buf->off, val, size);
    buf->off+=size;
}

int main(int argc,char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    //init cache
    struct cache cache;
    cache.cap = MAX_CACHE_SIZE;
    cache.size = 0;
    cache.head.next = &cache.tail;
    cache.tail.prev = &cache.head;

    //init rwlock
    pthread_rwlock_t rw_lock;
    pthread_rwlock_init(&rw_lock, NULL);

    // 创建套接字
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt SO_REUSEPORT failed");
        exit(1);
    }

    // 设置服务器地址
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 绑定套接字
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        perror("Socket binding failed");
        close(server_socket);
        return 1;
    }

    // 监听连接请求
    if (listen(server_socket, 10) == -1) {
        perror("Listening failed");
        close(server_socket);
        return 1;
    }
    //printf("%s", user_agent_hdr);

    while (1) {
        struct sockaddr_in cli_addr;
        unsigned int cli_len = sizeof(cli_addr);
        int cli_fd;
        if((cli_fd = accept(server_socket, (struct sockaddr*)&cli_addr, &cli_len))<0)
        {
            perror("accept failed");
        }


        struct handle_cli_param param;
        param.cache = &cache;
        param.cli_fd = cli_fd;
        param.rw_lock = &rw_lock;

        pthread_t cli_thread;
        if(pthread_create(&cli_thread, NULL, handle_client,&param)!=0)
        {
            perror("create thread failed\n");
            handle_client(&param);
        }else {
            pthread_detach(cli_thread);
        }

    }
    return 0;
}


void read_requesthdrs(rio_t *rp,struct out_buf *out,char *host) 
{
    char buf[MAXLINE],hd_key[MAXLINE],hd_val[MAXLINE];
    int ho=0,con=0,pcon=0,uagent=0;

    rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        rio_readlineb(rp, buf, MAXLINE);
        if(strcmp(buf, "\r\n")==0)
            break;
        sscanf(buf, "%s %s",hd_key,hd_val);
        printf("header %s %s\n",hd_key,hd_val);

        if(!strcasecmp(hd_key, "Host:"))
        {
            ho = 1;
        } else if(!strcasecmp(hd_key, "Connection:"))
        {
            con = 1;
            strcpy(hd_val, "close");
        } else if(!strcasecmp(hd_key, "Proxy-Connection:"))
        {
            pcon = 1;
            strcpy(hd_val, "close");
        } else if(!strcasecmp(hd_key, "User-Agent:"))
        {
            uagent = 1;
            strcpy(hd_val, user_agent_hdr);
        }
        sprintf(buf, "%s %s\r\n",hd_key,hd_val);
        write_buf(out,buf,strlen(buf));
    }

    if(!ho)
    {
        strcpy(hd_key, "Host:");
        sprintf(buf, "%s %s\r\n",hd_key,host);
        write_buf(out, buf, strlen(buf));
    }
    if(!con)
    {
        write_buf(out, "Connection: close\r\n", sizeof("Connection: close"));
    }
    if(!pcon)
    {
        write_buf(out, "Proxy-Connection: close\r\n", sizeof("Proxy-Connection: close"));
    }
    if(!uagent)
    {
        strcpy(hd_key, "User-Agent");
        sprintf(buf, "%s: %s\r\n",hd_key,user_agent_hdr);
        write_buf(out, buf, strlen(buf));
    }
    return;
}

ssize_t do_forward(struct node *nd,char *host,struct out_buf *out,int cli_fd)
{
    int sockfd,port;
    struct addrinfo hints,*res,*p;
    char *split = strchr(host, ':');
    char port_s[10];
    if(!split)
        port = 80;
    else {
        if(!(port = atoi(split+1)))
        {
            port = 80;
        } else {
            *split = '\0';
        }
    }
    sprintf(port_s, "%d",port);

    sockfd = open_clientfd(host, port_s);

    char res_buf[4096];
    ssize_t n;
    size_t cnt = 0;
    //printf("%s\n",out->out);
    rio_writen(sockfd, (void*)out->out, out->off);

    char *object[MAX_OBJECT_SIZE];
    while ((n=rio_readn(sockfd, res_buf, 4096))>0) {
        printf("read server %ld\n",n);
        if(cnt+n<=MAX_OBJECT_SIZE)
        {
            memcpy(object+cnt, res_buf, n);
        }
        if(rio_writen(cli_fd, res_buf, n)<0)
            return -1;
        cnt+=n;
    }
    if(n<0)
        return -1;
    if(cnt<MAX_OBJECT_SIZE)
    {
        nd->data = (char*)malloc(cnt);
        memcpy(nd->data, object, cnt);
        nd->len = cnt;
    }
    printf("forward %ld bytes\n",cnt);
    return n;
}

int parse_uri(char *uri,char *host, char *resource)
{
    char *start = uri,*end = uri+strlen(uri);
    if(strncasecmp("http://", uri,strlen("http://"))==0)
    {
        start += strlen("http://");
    }

    if(strncasecmp("https://", uri,strlen("https://"))==0)
    {
        start += strlen("https://");
    }

    char *split = strchr(start, '/');
    if(split)
    {
        strncpy(host, start, split-start);
        strcpy(resource, split);
    } else {
        strcpy(host, start);
        strcpy(resource, "/");
    }
    return 0;
}

int handle_http(rio_t *rp,struct cache *cache,pthread_rwlock_t* rw_lock)
{
    struct out_buf out;
    out.off = 0;
    out.out = (char*)malloc(MAXBUF);
    ssize_t n;
    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],host[MAXLINE],resource[MAXLINE];
    if((n = rio_readlineb(rp, buf, MAXLINE))<0)
    {
        perror("read failed");
        free(out.out);
        return -1;
    }
    if(!n)
    {
        free(out.out);
        return -1;
    }

    sscanf(buf, "%s %s %*s", method, uri);
    printf("method and uri %s %s\n",method,uri);
    if(strcasecmp(method, "GET"))
    {
        perror("not implement this method");
        free(out.out);
        return -1;
    }

    //读取缓存
    size_t hash = str_hash(uri);
    pthread_rwlock_rdlock(rw_lock);
    struct node *nd = find_cache(cache, uri, hash);
    pthread_rwlock_unlock(rw_lock);
    if(nd)
    {
        rio_writen(rp->rio_fd, nd->data, nd->len);
        free(out.out);
        return 0;
    }
    //处理uri
    if(parse_uri(uri, host, resource)<0)
    {
        perror("parse uri failed\n");
        free(out.out);
        return -1;
    }
    printf("host %s file %s\n",host,resource);
    sprintf(buf, "%s %s %s\r\n",method,resource,version_line);
    write_buf(&out, buf, strlen(buf));
    
    //处理请求头，获得host
    printf("start read header\n");
    read_requesthdrs(rp, &out, host);
    printf("host %s\n",host);
    //转发
    nd = (struct node*)malloc(sizeof(struct node));
    nd->len = 0;
    if(do_forward(nd,host, &out, rp->rio_fd)<0)
    {
        perror("forward error");
        free(out.out);
        return -1;
    }
    if(nd->len>0)
    {
        nd->key = (char*)malloc(strlen(uri)+1);
        strcpy(nd->key,uri);
        nd->hash = str_hash(nd->key);
        pthread_rwlock_wrlock(rw_lock);
        put_cache(cache, nd);
        pthread_rwlock_unlock(rw_lock);
    }
    free(out.out);
    return 0; 
}

void* handle_client(void* param)
{
    struct handle_cli_param *real_param = (struct handle_cli_param*)param;
    rio_t rp;
    rio_readinitb(&rp,real_param->cli_fd);
    handle_http(&rp,real_param->cache,real_param->rw_lock);
    return NULL;
}

