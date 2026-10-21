#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct cache_entry
{
    char uri[MAXLINE];//这个响应对应哪个完整 URI

    unsigned char *object;//完整 HTTP 响应的二进制字节
    size_t size;//object 的实际字节数

    unsigned long last_used;//最近一次访问时间，用于 LRU

    struct cache_entry *next;//下一个缓存项
}cache_entry_t;

//全局变量，所有工作线程共享
static cache_entry_t *cache_head = NULL;//链表中的第一个缓存项
static size_t cache_size = 0;//当前缓存总字节数
static unsigned long cache_clock = 0;//不断增加的 LRU 时间

static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;//防止多个线程同时破坏缓存

int parse_uri(const char *uri,char *hostname,char *port,char *path)
{   
    /*
    *先区分host_port和path
    */
    char host_port[MAXLINE];

    const char *host_begin;
    const char *path_begin;
    char *port_begin;
    //目前只接受http:// URI

    if(strncasecmp(uri,"http://",7)!=0){
        return -1;
    }
    //跳过开头的http://
    host_begin = uri+7;
    //寻找资源路径开头的
    path_begin = strchr(host_begin,'/');

    if(path_begin!=NULL){
        size_t host_port_length = path_begin - host_begin;
        if(host_port_length>=sizeof(host_port)){
            return -1;
        }
        memcpy(host_port,host_begin,host_port_length);
        host_port[host_port_length]='\0';
        snprintf(path,MAXLINE,"%s",path_begin);
    }
    else{// 没有路径时请求根路径/
        snprintf(host_port, MAXLINE, "%s", host_begin);
        snprintf(path, MAXLINE, "/");
    }
    /*
    *接下来区分host,port
    */
    port_begin=strchr(host_port,':');
    if(port_begin!=NULL){
        *port_begin='\0';//辅助snprintf找到字符串的结束位置
        //eg:127.0.0.1:8000-->127.0.0.1 \0 8000

        snprintf(hostname,MAXLINE,"%s",host_port);
        snprintf(port,MAXLINE,"%s",port_begin+1);
    }
    else{
        snprintf(hostname,MAXLINE,"%s",host_port);
        snprintf(port,MAXLINE,"80");//默认是80端口
    }

    if(hostname[0]=='\0'||port[0]=='\0'){
        //请求失败或者根本连hostname都没有
        return -1;
    }
    return 0;
}

void doit(int clientfd){
    rio_t client_rio;//读取缓冲区
    rio_t server_rio;

    char buf[MAXLINE];
    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];

    char hostname[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
    //从客户端获取基本的method,uri(http://hostname:port/path),version
    Rio_readinitb(&client_rio,clientfd);

    if(rio_readlineb(&client_rio,buf,MAXLINE)==0){
        //读取相应行
        return;
    }
    if(sscanf(buf,"%s %s %s",method,uri,version)!=3){
        //从相应行中一次读取方法,uri,http版本
        return;
    }
    if(strcasecmp(method,"GET")!=0){
        return;
    }
    if(parse_uri(uri, hostname, port, path)<0){
        return;
    }

    //将客户端发送的请求重新组织转发给服务端

    int serverfd=open_clientfd(hostname,port);//请求和hostname客户端的port端口建立连接
    if(serverfd<0){
        return;
    }
    snprintf(buf,sizeof(buf),"GET %s HTTP/1.0\r\n",path);//既然已经和tiny的某个端口连接上，就只用提供资源路径即可
    Rio_writen(serverfd,buf,strlen(buf));//转发请求行

    //转发请求头
    int has_host = 0;

    while(Rio_readlineb(&client_rio, buf, MAXLINE)>0){
        if(!strcmp(buf,"\r\n")){
            break;
        }

        if(!strncasecmp(buf,"Host:",5)){
            //这是host请求头
            has_host=1;
            Rio_writen(serverfd,buf,strlen(buf));
            continue;
            
        }
        if(!strncasecmp(buf,"User-Agent:",11)||!strncasecmp(buf,"Connection:",11)||!strncasecmp(buf,"Proxy-Connection:",17)){
            continue;
            //不转发的请求头
        }
        Rio_writen(serverfd,buf,strlen(buf));//其他请求头就直接转发
    }
    if(!has_host){
        if(!strcmp(port,"80")){
            snprintf(buf,sizeof(buf),"Host: %s\r\n",hostname);
        }
        else{
            snprintf(buf,sizeof(buf),"Host:%s:%s\r\n",hostname,port);
        }
        Rio_writen(serverfd,buf,strlen(buf));//转发补全的请求头
    }
    //固定结尾
    snprintf(
        buf,
        sizeof(buf),
        "%s"
        "Connection: close\r\n"
        "Proxy-Connection: close\r\n"
        "\r\n",
        user_agent_hdr
    );
    //转发tiny的响应
    Rio_writen(serverfd, buf, strlen(buf));
    Rio_readinitb(&server_rio,serverfd);//以后通过server_rio从serverfd读取Tiny的响应
    ssize_t n;
    while((n=Rio_readnb(&server_rio,buf,MAXBUF))>0){//无需区分响应头/体，直接转发
        Rio_writen(clientfd,buf,n);//不能用strlen(buf),因为响应体可能是图片包含'\0',会导致转发提前终止
    }
    Close(serverfd);
}

void *thread(void *arg)
{
    Pthread_detach(Pthread_self());//采用detach由操作系统自动回收线程
    int clientfd =*(int *)arg;
    Free(arg);//本质上就是释放保存clientfd数值的临时堆内存
    
    doit(clientfd);
    Close(clientfd);
    
    return NULL;
}

int main(int argc,char **argv)
{

    int listenfd;
    pthread_t tid;
    struct sockaddr_storage clientaddr;
    socklen_t clientlen;

    if(argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen =sizeof(clientaddr);
        int * clientfd_ptr =Malloc(sizeof(int));
        *clientfd_ptr=Accept(listenfd,(SA *)&clientaddr,&clientlen);
        Pthread_create(&tid,NULL,thread,clientfd_ptr);
    }
    return 0;
}
