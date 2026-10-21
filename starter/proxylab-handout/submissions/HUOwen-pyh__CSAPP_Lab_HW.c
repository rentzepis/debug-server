#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void sockParser(int fd);
static FILE* logfp;
static int logfd;

int main(int argc, char* argv[])
{
    logfp = fopen("log.txt","a");
    logfd = fileno(logfp);
    dup2(logfd,1);
    dup2(logfd,2);

    
    if(argc!=2){
        fprintf(stderr, "Parameter Number Incorrect: %s\n", argv[0]);
        exit(1);
    }

    fprintf(stdout,"Port is %s\n", argv[1]);
    fflush(stdout);

    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    listenfd = Open_listenfd(argv[1]);
    char clienthostname[MAXLINE], clientport[MAXLINE];

    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        Getnameinfo((SA*)&clientaddr,clientlen,clienthostname,MAXLINE,clientport,MAXLINE,0);
        fprintf(stdout,"Connected to (%s %s)\n",clienthostname,clientport);
        fflush(stdout);
        sockParser(connfd);
        Close(connfd);
    }

    
    fclose(logfp);
    exit(0);
}

void sockParser(int fd){
    rio_t rio, acceptrio;
    int clientfd;
    char serveport[16];
    int n;
    char buf[MAXLINE];
    char* pos = buf;
    
    Rio_readinitb(&rio,fd);
    while(n = rio_readlineb(&rio,pos,MAXLINE)){
        rio_writen(logfd, pos, n);
        pos += n;
        if(n>=0)continue;
        printf("Read fd error\n");
        fflush(stdout);
        exit(1);
    }

    pos = buf;
    pos += 21;
    for(n = 0;(*pos)<='9'&&(*pos)>='0';n++){
        serveport[n] = (*pos);
        pos++;
    }
    serveport[n] = 0;
    if((clientfd = Open_clientfd("127.0.0.1",serveport)) < 0){
        printf("Connect server error\n");
        fflush(stdout);
    }
    printf("Client Number is (%s %d)\n",serveport,clientfd);
    fflush(stdout);

    int slashcnt = 0;
    for(int i = 3;;i++){
        if((*(buf+i)) == '/')slashcnt ++;
        if(slashcnt == 3)break;
        *(buf + i) = ' ';
    }
    
    Rio_readinitb(&acceptrio,clientfd);
    if((n = rio_writen(clientfd,buf,strlen(buf)))<0){
        printf("Write fd error: %u %u\n",n,strlen(buf));
        fflush(stdout);
        exit(1);
    }
    while(n = rio_readn(clientfd,buf,MAXLINE-2)){
        if(n<0){
            printf("Accept serverinfo error\n");
            fflush(stdout);
            break;
        }
        rio_writen(logfd, buf, n);
        rio_writen(fd, buf, n);
    }
    //rio_writen(fd, endline, strlen(endline));

    printf("client serve ended\n");
    fflush(stdout);
    Close(clientfd);
    return;
}





