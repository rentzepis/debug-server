#include <stdio.h>
#include "csapp.h"
#include <string.h>
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_LINE 1024
struct Node{
    char uri[MAX_LINE];
    struct Node* succ,*prev;
    char* content;
    int size;
};
static struct Node* head=0,*tail=0;
static int total_size=0;
struct Node* read_cache(char* uri){
    struct Node* cur=head->succ;
    while(cur){
        if(strcmp(cur->uri,uri)==0){
            return cur;
        }
        cur=cur->succ;
    }
    return 0;
}
void update(struct Node* newhead){
    newhead->prev->succ=newhead->succ;
    newhead->succ->prev=newhead->prev;
    newhead->prev=head;
    newhead->succ=head->succ;
    head->succ->prev=newhead;
    head->succ=head;
}
void eviction(){
    struct Node* cur=tail->prev;
    while(total_size>=MAX_CACHE_SIZE){
        struct Node* tmp=cur->prev;
        free(cur->content);
        free(cur);
        cur=tmp;
    }
    cur->succ=tail;
    tail->prev=cur;
}
//踢到能装下为止
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
int read_and_parse(rio_t* rio_toclient,char* content,char* host){
    char buf[MAX_LINE];
    strcat(content,user_agent_hdr);
    strcat(content,"Connection: close\r\n");
    strcat(content,"Proxy-Connection: close\r\n");
    int withport=0;
    while(1){
        if(rio_readlineb(rio_toclient,buf,MAX_LINE)<=0){
            printf("Read from socket failed.\n");
            return -1;
        }
        char head[MAX_LINE]="";
        char Port[MAX_LINE]="";
        sscanf(buf,"%s",head);
        if(!strcmp(head,"Host:")){
            int len=0,len2=-1,i,strl=strlen(head);
            for(i=6;i<strl;i++){
                if(head[i]==':'){
                    len2++;
                    withport=1;
                    continue;
                }
                if(len2==-1)host[len++]=head[i];
                else Port[len2++]=head[i];
            }
            head[len]='\0';
            if(len2!=-1)Port[len2]='\0';
        }
        else if(!strcmp(head,"Connection:")||!strcmp(head,"Proxy-Connection:")||!strcmp(head,"User-Agent:")){
            continue;
        }
        else if(!strcmp(buf,"\r\n")){
            strcat(content,"Host: ");
            strcat(content,host);
            if(withport){
                strcat(content,":");
                strcat(content,Port);
            }
            strcat(content,"\r\n\r\n");
            return 1;
        }
        else{
            strcat(content,buf);
        }
        //printf("%s",buf);
    }
}
sem_t mutex;
void* thread(void* vargp){
    if(pthread_detach(pthread_self())){
        printf("thread_detach failed\n");
        fflush(stdout);
        return 0;
    }
    int connfd=(int)vargp;
    rio_t rio_toclient;
    char buf[MAX_LINE],method[MAX_LINE],uri[MAX_LINE],version[MAX_LINE];
    char host[MAX_LINE]="",port[MAX_LINE]="",path[MAX_LINE]="";
    //getnameinfo((struct sockaddr*)&clientaddr,clientlen,host,MAXLINE,port,MAXLINE,0);
    //printf("%s %s \n",host,port);
    rio_readinitb(&rio_toclient,connfd);
    if(rio_readlineb(&rio_toclient,buf,MAX_LINE)<0){
        printf("Receive from client failed.\n");
        fflush(stdout);
        close(connfd);
        return 0;
    }
    sscanf(buf,"%s %s %s",method,uri,version);
    size_t strl=strlen(uri);
    int len=0,len2=-1,i;
    for(i=(strstr(uri,"http://")?7:0);i<strl&&uri[i]!='/';i++){
        if(uri[i]==':'){
            len2++;
            continue;
        }
        if(len2==-1)host[len++]=uri[i];
        else port[len2++]=uri[i];
    }
    strcpy(path,uri+i);
    host[len]='\0';
    if(len2==-1)strcpy(port,"80");
    else port[len2]='\0';
    char content[MAXLINE]="";
    sprintf(content,"GET %s HTTP/1.0\r\n",path);

    int local_size=0;
    char* local_content=0;
    P(&mutex);
    struct Node* res=read_cache(uri);
    if(res){
        update(res);
        local_size=res->size;
        local_content=malloc(local_size);
        memcpy(local_content,res->content,local_size);
    }
    V(&mutex);
    if(local_content){
        rio_writen(connfd,local_content,local_size);
        free(local_content);
        close(connfd);
        return 0;
    }
    
    if(read_and_parse(&rio_toclient,content,host)<=0){
        close(connfd);
        return 0;
    }
    int clientfd;
    printf("%s PORT:%s HOST:%s PATH:%s\n",content,port,host,path);
    fflush(stdout);

    if((clientfd=open_clientfd(host,port))==-1){
        printf("Can not connet to server %s:%s\n",host,port);
        fflush(stdout);
        close(connfd);
        return 0;
    }

    if(rio_writen(clientfd,content,strlen(content))!=strlen(content)){
        printf("Send to server error\n");
        fflush(stdout);
        close(connfd);
        close(clientfd);
        return 0;
    }
    
    rio_t rio_toserver;
    rio_readinitb(&rio_toserver,clientfd);
    int buf_size=0;
    char* buffer=malloc(MAX_OBJECT_SIZE);
    while(1){
        char response[MAXLINE];
        int res=rio_readnb(&rio_toserver,response,MAXLINE);
        if(res==0){
            break;
        }
        else if(res<0){
            printf("Receive from server error\n");
            fflush(stdout);
            break;
        }
        if(buf_size<MAX_OBJECT_SIZE)memcpy(buffer+buf_size,response,res);
        int size=rio_writen(connfd,response,res);
        buf_size+=size;
        if(size!=res){
            printf("Send to client error\n");
            fflush(stdout);
            break;
        }
    }
    if(buf_size<MAX_OBJECT_SIZE){
        
        struct Node* newhead=malloc(sizeof(struct Node));
        newhead->prev=head;
        
        newhead->content=buffer;
        strcpy(newhead->uri,uri);
        P(&mutex);
        newhead->succ=head->succ;
        newhead->size=buf_size;
        total_size+=buf_size;
        eviction();
        head->succ->prev=newhead;
        head->succ=newhead;
        V(&mutex);
    }
    else free(buffer);
    close(connfd);
    close(clientfd);
    return 0;
}

int main(int argc,char* argv[])
{
    struct sockaddr_storage clientaddr;
    head=malloc(sizeof(struct Node));
    tail=malloc(sizeof(struct Node));
    head->succ=tail;
    head->prev=0;
    tail->prev=head;
    tail->succ=0;
    tail->content=head->content=0;
    tail->size=head->size=0;
    sem_init(&mutex,0,1);

    socklen_t clientlen=sizeof(struct sockaddr_storage);
    if(argc!=2){
        printf("usage: %s <port>\n",argv[0]);
        return 0;
    }

    int listenfd,connfd;
    signal(SIGPIPE,SIG_IGN);
    while((listenfd=open_listenfd(argv[1]))==-1){
        printf("Listen port %s failed.Retrying...\n",argv[1]);
        sleep(1);
    }
    pthread_t tid;
    while(1){
        if((connfd=accept(listenfd,(struct sockaddr*)&clientaddr,&clientlen))==-1){
            printf("Accept socket failed.Retrying...\n");
            fflush(stdout);
            continue;
        }
        if(pthread_create(&tid,0,thread,(void*)connfd)){
            close(connfd);
            printf("Create thread failed\n");
            fflush(stdout);
        }
        printf("TID:%lu\n",tid);
        fflush(stdout);
    }
    struct Node* pre=head,*cur=head->succ;
    while(cur){
        free(pre->content);
        free(pre);
        pre=cur;
        cur=cur->succ;
    }
    free(pre->content);
    free(pre);
    return 0;
}