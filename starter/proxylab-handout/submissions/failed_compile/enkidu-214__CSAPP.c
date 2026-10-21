#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define BUFFERSIZE 100
#define NTHREADS 16
#define MAX_CACHE 10
#define MAX_READERS 4
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

struct Uri{
    char host[MAXLINE]; //hostname
    char port[MAXLINE]; //端口
    char path[MAXLINE]; //路径 
};

// Cache结构
typedef struct
{
    char obj[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int LRU;
    int isEmpty;

    int read_cnt; //读者数量
    sem_t w;      //保护 Cache
    sem_t mutex;  //保护 read_cnt

} block;

typedef struct
{
    block data[MAX_CACHE];
    int num;
} Cache;

Cache cache;
sbuf_t s_buff;
                                                                 
void doit(int fd);
int parse_uri(char *uri, struct Uri*);
void build_header(char *http_header, struct Uri *uri_data, rio_t *client_rio);

void cache_init(Cache *cache_new);
int in_cache(char * cache_tag);
void write_cache(char* cache_tag,char* cache_buf);

void sigpipe_handler(int signum) {
  printf("receive sigpipe.");
  exit(1); 
}

void thread(){
    Pthread_detach(pthread_self());
    while(1){
        int connfd = sbuf_remove(&s_buff);
        doit(connfd);                                             // line:netp:tiny:doit
        Close(connfd); 
    }
} 

                
// 程序运行需要传入http服务监听的端口信息，默认80
int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];
    pthread_t tid;
    
    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    
    signal(SIGPIPE, sigpipe_handler);
    cache_init(&cache);
    sbuf_init(&s_buff,BUFFERSIZE);
    for(int i = 0; i < NTHREADS; i++)
    {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        //connfd可以理解为一个可以接受重定向输出流的描述符
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
        sbuf_insert(&s_buff, connfd);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int connfd)
{
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char server[MAXLINE];
    rio_t rio, server_rio;
    int cache_idx=-1;
    char cache_tag[MAXLINE];
    struct Uri *curr_uri = (struct Uri *)malloc(sizeof(struct Uri));
    /* Read request line and headers */
    //rio为一个线程安全的读缓存
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);             // line:netp:doit:readrequest
    sscanf(buf, "%s %s %s", method, uri, version); // line:netp:doit:parserequest
    strcpy(cache_tag,uri);

    if (strcasecmp(method, "GET"))
    { // line:netp:doit:beginrequesterr
        printf("Proxy does not implement the method");
        return;
    }                       // line:netp:doit:endrequesterr

    parse_uri(uri,curr_uri);

    //我都不知道这个是啥,看源码才知道是将url请求输入进server
    build_header(server, curr_uri, &rio);
    
    //如果缓存中有数据
    if ((cache_idx=in_cache(cache_tag)) != -1){
    	P(&cache.data[cache_idx].mutex);
        cache.data[cache_idx].read_cnt++;
        //当且仅当第一次读的时候需要加写锁
        if(cache.data[cache_idx].read_cnt == 1){
            P(&cache.data[cache_idx].w);
        }
        V(&cache.data[cache_idx].mutex);

        Rio_writen(connfd, cache.data[cache_idx].obj, sizeof(cache.data[cache_idx].obj));

        P(&cache.data[cache_idx].mutex);
        cache.data[cache_idx].read_cnt--;
        //当且仅当第一次读的时候需要加写锁
        if(cache.data[cache_idx].read_cnt == 0){
            V(&cache.data[cache_idx].w);
        }
        V(&cache.data[cache_idx].mutex);

    	return;
    }
    //以下全抄
    int serverfd = Open_clientfd(curr_uri->host, curr_uri->port);
    if (serverfd < 0)
    {
        printf("connection failed\n");
        return;
    }
    //转发给服务器
    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, server, strlen(server));

    size_t n;
    char cache_buf[MAX_OBJECT_SIZE];
    size_t cache_n=0;
    //回复给客户端
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
    {
        cache_n += n;
        if(cache_n < MAX_OBJECT_SIZE){
            strcat(cache_buf,buf);
        }
        printf("proxy received %d bytes,then send\n", (int)n);
        Rio_writen(connfd, buf, n);
    }
    if(cache_n < MAX_OBJECT_SIZE){
        write_cache(cache_tag, cache_buf);
    }
    //关闭服务器描述符
    Close(serverfd);
}

/*void build_header(char *server,struct Uri* uri_data,rio_t *rp){
    sprintf(server,"GET %s HTTP/1.0\r\n",uri_data->path);
    sprintf(server,"Host: %s\r\n",uri_data->host);
    sprintf(server,"User-Agent: close\r\n",user_agent_hdr); 
    sprintf(server,"Connection: close\r\n");
    sprintf(server,"Proxy-Connection: close\r\n");   
}*/

//以下为参考代码，质量高了一个量级
void build_header(char *http_header, struct Uri *uri_data, rio_t *client_rio)
{
    char *User_Agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    char *conn_hdr = "Connection: close\r\n";
    char *prox_hdr = "Proxy-Connection: close\r\n";
    char *host_hdr_format = "Host: %s\r\n";
    char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
    char *endof_hdr = "\r\n";

    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
    sprintf(request_hdr, requestlint_hdr_format, uri_data->path);
    //这里和我的处理方式完全不一样，不能只是获取一个host和path就传给服务器，而是要将别的信息也传输过去
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
    {
        if (strcmp(buf, endof_hdr) == 0)
            break; /*EOF*/
	//比较字符串的前n个字符
        if (!strncasecmp(buf, "Host", strlen("Host"))) /*Host:*/
        {
            strcpy(host_hdr, buf);
            //if(strlen(uri_data->host) == 0)
            	//sprintf(uri_data->host,host_hdr_format,);
            continue;
        }

        if (!strncasecmp(buf, "Connection", strlen("Connection")) && !strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) && !strncasecmp(buf, "User-Agent", strlen("User-Agent")))
        {
            strcat(other_hdr, buf);
        }
    }
    //这个方法很好，只有在host没有数据的时候，才会从Uri中传递数据
    if (strlen(host_hdr) == 0)
    {
        sprintf(host_hdr, host_hdr_format, uri_data->host);
    }
    sprintf(http_header, "%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            User_Agent,
            other_hdr,
            endof_hdr);

    return;
}

/* $end doit */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri,struct Uri* curr_uri)
{
    char *host_name = strstr(uri,"//");
    //当前uri只包含www.baidu.com/sdds
    if(host_name == NULL){
        char* path_name = strstr(uri,"/");
        //第一次写的时候没有加判断NULL
        if(path_name != NULL){
            //一开始我设置的是path_name+1,但是注意这个path是需要/的前缀的
            strcpy(curr_uri->path,path_name);
        }
        strcpy(curr_uri->port,"80");
        return;
    }     
    else{
        //检查是否存在端口号
        char *port_name = strstr(host_name+2,":");
        if(port_name == NULL){
            char* path_name = strstr(host_name + 2,"/");
            if(path_name != NULL){
                strcpy(curr_uri->path,path_name);
            }
            strcpy(curr_uri->port,"80");
            *path_name = '\0';
        }
        else{            
            int tmp;
            sscanf(port_name + 1, "%d%s", &tmp, curr_uri->path);
            sprintf(curr_uri->port, "%d", tmp);
            *port_name = '\0';
        }
        strcpy(curr_uri->host,host_name+2);
    }
}
/* $end parse_uri */
void cache_init(Cache *cache_new){
    cache_new->num=MAX_CACHE;
    for(int i=0;i<MAX_CACHE;i++){
        //这里陷入沉思，不知道该如何初始化非指针结构体，但是其实不需要，因为声明的就是一个完整的结构体，已经分配了空间
        cache_new->data[i].isEmpty=1;
        cache_new->data[i].LRU=0;
        cache_new->data[i].read_cnt=0;
        Sem_init(&cache_new->data[i].w, 0, 1);
        Sem_init(&cache_new->data[i].mutex, 0, MAX_READERS);
    }
} 

void write_cache(char* cache_tag,char* cache_buf){
    int write_idx=-1;
    int max_LRU=0;
    for(int i=0;i<MAX_CACHE;i++){
        if(cache.data[i].isEmpty){
            write_idx=i;
            break;
        }
        else{
            if(max_LRU < cache.data[i].LRU){
                max_LRU = cache.data[i].LRU;
                write_idx = i;
            }
        }
    }
    //设置检查点
    if(write_idx == -1){
        printf("Unexpected ERROR.");
        return;
    }
    
    P(&cache.data[write_idx].w);
    cache.data[write_idx].LRU=0;
    cache.data[write_idx].isEmpty=0;
    strcpy(cache.data[write_idx].obj, cache_buf);
    strcpy(cache.data[write_idx].uri, cache_tag);
    
    //所有的写操作必须加锁
    for(int i=0;i<MAX_CACHE;i++){
        if(i!=write_cache && !cache.data[i].isEmpty)
            cache.data[i].LRU++;
    }

    V(&cache.data[write_idx].w);
}

int in_cache(char * cache_tag){
    for(int i=0;i<MAX_CACHE;i++){
        if(!cache.data[i].isEmpty && !strcmp(cache_tag,cache.data[i].uri)){
            return i;
        }
    }
    return -1;
}
