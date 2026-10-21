/* 1) Use producer-consumer model to have the main thread push client request to a buffer, and worker threads extract request
 * from the buffer, which avoids the overheads of creating threads. 
 * 2) implement the cache as a linkedlist with a hashtable to quickly find the content we want.
 * advantage: average O(1) time of search and not waste memory.
 * disadvantage: insert time still cannot be measured, but requires only O(1) time if no eviction is required
 * and if we assume that most object have roughly the same size, then we only need to evict constant item before
 * we can insert, which is also O(1) time. Besides, since we add a hash table, the metadata is larger.
 */
#include <stdio.h>
#include "csapp.h"
#include "map.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREAD 8
#define SBUFSIZE 16

typedef struct {
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

typedef struct {
    HashMap hm;
    CacheNode* header;
    CacheNode* footer;
    volatile int readcnt;
    int left_size;
    sem_t mutex;
    sem_t w;
} Cache;

void doit(int fd);
void init_headers(char *header);
int read_requesthdrs(rio_t *rp,char *header);
int parse_url(char *url,char *hostname,char *reqeust_line,char *port);
void send_request(int serverfd,char *request_line,char *header);
void redirect_response(int clientfd,rio_t *rio_server,char *URL);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

void sbuf_init(sbuf_t *sp,int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp,int item);
int sbuf_remove(sbuf_t *sp);
void *thread(void *vargp);

void cache_init(Cache *cache_ptr);
CacheNode *cache_find(Cache* cache_ptr,char *URL);
void send_from_cache(Cache *cache_ptr,CacheNode *curr,int clientfd);
void cache_insert(Cache *cache_ptr,char *URL,char *content,int size);
void cache_deinit(Cache *cache_ptr);
void free_CacheNode(CacheNode* cl);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.16; rv:85.0) Gecko/20100101 Firefox/85.0\r\n";
static sbuf_t sbuf;
static Cache cache;

int main(int argc,char* argv[])
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    /* Check the command line args*/
    if(argc != 2){
        fprintf(stderr,"usage: %s <port>\n",argv[0]);
        exit(1);
    }
    Signal(SIGPIPE,SIG_IGN);

    listenfd = Open_listenfd(argv[1]);

    cache_init(&cache);
    sbuf_init(&sbuf,SBUFSIZE);

    for(int i=0;i<NTHREAD;i++)
        pthread_create(&tid,NULL,thread,NULL);

    while(1){
        clientlen = sizeof(clientaddr);
        connfd=Accept(listenfd,(SA *)&clientaddr,&clientlen);
        printf("connect to client on file descriptor %d\n",connfd);
        sbuf_insert(&sbuf,connfd);
    }
    return 1;
}

void *thread(void *vargp){
    pthread_detach(pthread_self());
    while(1){
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        close(connfd);
    }
}

void doit(int fd){
    char buf[MAXLINE],method[MAXLINE],url[MAXLINE],version[MAXLINE];
    char header[MAXLINE]="",hostname[MAXLINE],uri[MAXLINE];
    char port[MAXLINE]="80",request_line[MAXLINE];
    int has_host,serverfd;
    rio_t rio_client,rio_server;

    /* Read request line */
    rio_readinitb(&rio_client,fd);
    if(!rio_readlineb(&rio_client,buf,MAXLINE))
        return;
    /* divide the request line into request method, URL and HTTP version */
    if(sscanf(buf,"%s %s %s",method,url,version)!=3)
        return;
    /* Check if the request is a GET request*/
    if(strcasecmp(method,"GET")){
        clienterror(fd,method,"501","Not Implemented",
                    "the proxy does not implement this method");
        return;
    }

    /* Parse the URL into hostname and uri and even the port */
    if(!parse_url(url,hostname,uri,port)){
        clienterror(fd,method,"400","Bad request","Hostname is invalid");
        return;
    }
    sprintf(request_line,"GET %s HTTP/1.0\r\n",uri);
    //initialize the header
    init_headers(header);
    
    //fill the header
    has_host=read_requesthdrs(&rio_client,header);
    //handle the Host header
    if(has_host==-1){
        clienterror(fd,method,"400","Bad request","Request header is invalid");
        return;
    }
    else if(has_host==0)
        sprintf(header,"%sHost: %s\r\n\r\n",header,hostname);
    else
        strcat(header,"\r\n");

    strcpy(url,hostname);
    strcat(url,uri);
    CacheNode* curr=cache_find(&cache,url);
    
    if(curr){
        send_from_cache(&cache,curr,fd);
        return;
    }   

    serverfd=open_clientfd(hostname,port);

    //cannot build connection 
    if(serverfd<0){
        clienterror(fd,method,"400","Bad request","Request is invalid");
        return;
    }

    rio_readinitb(&rio_server,serverfd);
    
    send_request(serverfd,request_line,header);

    redirect_response(fd,&rio_server,url);

    close(serverfd);
}

void init_headers(char *header){
    strcat(header,user_agent_hdr);
    strcat(header,"Connection: close\r\n");
    strcat(header,"Proxy-Connection: close\r\n");
}

int read_requesthdrs(rio_t *rp,char *header){
    char *pos,buf[MAXLINE];
    size_t size;
    int has_host=0;
    size=Rio_readlineb(rp,buf,MAXLINE);
    while(strcmp(buf,"\r\n")){
        pos=strchr(buf,':');
        if(!pos)
            return -1;
        *pos='\0';
        if(!strcmp(buf,"Host"))
            has_host=1;
        else if(!strcmp(buf,"Proxy-Connection")||!strcmp(buf,"Connection")||!strcmp(buf,"User-Agent")){
            size=Rio_readlineb(rp,buf,MAXLINE);
            continue;
        }
        *pos=':';
        strncat(header,buf,size);
        size=Rio_readlineb(rp,buf,MAXLINE);
    }
    return has_host;
}

int parse_url(char *url,char *hostname,char *uri,char *port){
    char *colon_pos, *slash_pos, *host_begin;
    host_begin=strstr(url,"http://");
    if(!host_begin)return 0;
    host_begin+=7;
    colon_pos=strchr(host_begin,':');
    if(colon_pos){
        *colon_pos='\0';
        strcpy(hostname,host_begin);
        *colon_pos=':';
        slash_pos=strchr(colon_pos+1,'/');
        if(!slash_pos){
            strcpy(port,colon_pos+1);
            strcpy(uri,"/");
            return 1;
        }
        *slash_pos='\0';
        strcpy(port,colon_pos+1);
        *slash_pos='/';
    }
    else{
        slash_pos=strchr(host_begin,'/');
        if(!slash_pos){
            strcpy(hostname,host_begin);
            strcpy(uri,"/");
            return 1;
        }
        *slash_pos='\0';
        strcpy(hostname,host_begin);
        *slash_pos='/';
    }
    strcpy(uri,slash_pos);
    return 1;
}
void send_request(int serverfd,char *request_line,char *header){
    rio_writen(serverfd,request_line,strlen(request_line));
    rio_writen(serverfd,header,strlen(header));
    printf("%s",request_line);
    printf("%s",header);
}

void redirect_response(int clientfd,rio_t *rio_server,char *URL){
    size_t size,total_size=0;
    char response[MAX_OBJECT_SIZE],buf[MAXLINE];
    char *pos=response;
    while((size=rio_readnb(rio_server,buf,MAXLINE))!=0){
        if(rio_writen(clientfd,buf,size)<0)return;
        total_size+=size;
        if(total_size<MAX_OBJECT_SIZE){
            memcpy(pos,buf,size);
            pos+=size;
        }
    }
    if(total_size<MAX_OBJECT_SIZE){
        cache_insert(&cache,URL,response,total_size);
    }
}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    if(rio_writen(fd, buf, strlen(buf))<0)return;
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    if(rio_writen(fd, buf, strlen(buf))<0)return;

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Proxy Error</title>");
    if(rio_writen(fd, buf, strlen(buf))<0)return;
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    if(rio_writen(fd, buf, strlen(buf))<0)return;
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    if(rio_writen(fd, buf, strlen(buf))<0)return;
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    if(rio_writen(fd, buf, strlen(buf))<0)return;
    sprintf(buf, "<hr><em>The Proxylab Web Proxy</em>\r\n");
    if(rio_writen(fd, buf, strlen(buf))<0)return;
}


void sbuf_init(sbuf_t *sp,int n){
    sp->buf = Calloc(n,sizeof(int));
    sp->n=n;
    sp->front=sp->rear=0;
    sem_init(&sp->mutex,0,1);
    sem_init(&sp->slots,0,n);
    sem_init(&sp->items,0,0);
}

void sbuf_deinit(sbuf_t *sp){
    free(sp->buf);
}
void sbuf_insert(sbuf_t *sp,int item){
    P(&sp->slots);
    P(&sp->mutex);
    sp->rear++;
    sp->buf[(sp->rear)%(sp->n)]=item;
    V(&sp->mutex);
    V(&sp->items);
}
int sbuf_remove(sbuf_t *sp){
    int item;
    P(&sp->items);
    P(&sp->mutex);
    sp->front++;
    item=sp->buf[(sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

void cache_init(Cache* cache_ptr){
    cache_ptr->header=NULL;
    cache_ptr->footer=NULL;
    map_init(&cache_ptr->hm);
    sem_init(&cache_ptr->mutex,0,1);
    sem_init(&cache_ptr->w,0,1);
    cache_ptr->readcnt=0;
    cache_ptr->left_size=MAX_CACHE_SIZE;
}

CacheNode *cache_find(Cache* cache_ptr,char *URL){
    int hash=MurmurOAAT32(URL);
    P(&cache_ptr->mutex);
    cache_ptr->readcnt++;
    if(cache_ptr->readcnt == 1)
        P(&cache_ptr->w);
    V(&cache_ptr->mutex);

    CacheNode *curr=map_find(&cache_ptr->hm,URL);

    P(&cache_ptr->mutex);
    cache_ptr->readcnt--;
    if(cache_ptr->readcnt==0)
        V(&cache_ptr->w);
    V(&cache_ptr->mutex);

    return curr;
}
void send_from_cache(Cache *cache_ptr,CacheNode *curr,int clientfd){
    P(&cache_ptr->w);

    rio_writen(clientfd,curr->content,curr->size);

    if(cache_ptr->header!=cache_ptr->footer){
        if(curr->prev) curr->prev->next=curr->next;
        if(curr->next) curr->next->prev=curr->prev;
        if(cache_ptr->footer==curr)cache_ptr->footer=curr->prev;
        curr->next=cache_ptr->header;
        curr->prev=NULL;
        cache_ptr->header=curr;
    }

    V(&cache_ptr->w);
}

void cache_insert(Cache *cache_ptr,char *URL,char *content,int size){
    P(&cache_ptr->w);

    int left_size=cache_ptr->left_size;
    if(left_size<size){
        CacheNode *curr=cache_ptr->footer;
        while(left_size<size){
            left_size+=curr->size;
            map_delete(&cache_ptr->hm,curr->URL);
            curr=curr->prev;
            free_CacheNode(curr->next);
        }
        cache_ptr->footer=curr;
        curr->next=NULL;
        cache_ptr->left_size=left_size;
    }
    cache_ptr->left_size-=size;
    CacheNode *curr=(CacheNode *)malloc(sizeof(CacheNode));
    CacheNode *header=cache_ptr->header;
    map_insert(&cache_ptr->hm,URL,curr);
    curr->prev=NULL;
    curr->next=header;
    if(header)header->prev=curr;
    cache_ptr->header=curr;
    if(!cache_ptr->footer)cache_ptr->footer=curr;
    curr->hash=MurmurOAAT32(URL);
    curr->size=size;
    curr->content=(char *)malloc(size);
    strcpy(curr->URL,URL);
    memcpy(curr->content,content,size);

    V(&cache_ptr->w);
}

void cache_deinit(Cache *cache_ptr){
    CacheNode *curr=cache_ptr->header;
    while(curr){
        map_delete(&cache_ptr->hm,curr->URL);
        curr=curr->next;
        free_CacheNode(curr->prev);
    }
}

void free_CacheNode(CacheNode* cl){
    free(cl->content);
    free(cl);
}