#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS    4
#define SBUFSIZE    16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
        "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
        "Firefox/10.0.3\r\n";
//static const char *proxy_port;

void doit(int fd);
void read_requesthdrs(rio_t *rp);
void make_request_to_server(int ptsfd,char* url, char* host, char* port, char* method, char* version, char* filename);
void parsingHeader(char* uri,char* host,char* port,char* filename);
void* thread(void* vargp);

sbuf_t sbuf; //
static CacheList* cacheList;
static sem_t mutex;

int main(int argc, char **argv)
{
    int listenfd, connfd; // 듣기 식별자, 연결식별자
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    cacheList = initCache();
    Sem_init(&mutex, 0, 1);
    if (argc != 2) // 에러검출
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }
    while (1)
    {
        clientlen = sizeof(clientaddr);
        // 연결식별자 포인터 말록, 엑셉트
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        // Getnameinfo : 클라이언트 hostname과 port를 채우고 accept 문자 적어줌
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd);
    }
    deleteCache(cacheList);
}

void* thread(void* vargp){
    //int connfd = *((int *) vargp);
    Pthread_detach(pthread_self());
    while (1){
        int connfd = sbuf_remove(&sbuf);
        doit(connfd);
        Close(connfd);
    }
}
void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], url[MAXLINE], version[MAXLINE], port[MAXLINE], host[MAXLINE], filename[MAXLINE];
    char response[MAX_OBJECT_SIZE];
    rio_t client_rio,server_rio;

    Rio_readinitb(&client_rio, fd);
    Rio_readlineb(&client_rio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s\n", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    strcpy(url, uri);
    parsingHeader(uri, host, port, filename);

    /* GET method만 받음 */
    if(strcasecmp(method, "GET") != 0) {
        sprintf(buf, "GET요청이 아닙니다.\r\n");
        Rio_writen(fd, buf, strlen(buf));
        return;
    }

    /* 클라이언트 to 프록시 서버 request 내용 받음 */
    printf("=======Receive Request From Client=======\n");
    read_requesthdrs(&client_rio);


    /*
     * 여기서 캐시에 데이터가 있으면 리턴
     * 요청 헤더의 파싱된 값들을 통해서 캐시 블록을 insert
     * */
    P(&mutex);
    char* ret = findCacheNode(cacheList, url);
    if (ret != NULL) {
        printf("=======Receive Request is In Cache=======\n");
        Rio_writen(fd, ret, MAX_OBJECT_SIZE);
        return;
    }
    V(&mutex);


    printf("=======Receive Request is Not In Cache=======\n");

    /* 프록시 서버 to 타겟 서버 처리*/
    int serverFd = Open_clientfd(host, port);
    Rio_readinitb(&server_rio, serverFd);
    printf("=======Send Request To Server=======\n");
    make_request_to_server(serverFd, uri, host, port, method, version, filename);
    printf("=======Receive Request From Server=======\n");
    Rio_readnb(&server_rio, response, MAX_OBJECT_SIZE);

    P(&mutex);
    insertCacheNode(cacheList, url, response);
    V(&mutex);
    printf("%s", response);
    Close(serverFd);



    /* 클라이언트 to 프록시 서버 내용 보냄 */
    printf("=======Send Response To Client=======\n");
    Rio_writen(fd, response, MAX_OBJECT_SIZE);
    printf("%s", response);
}

void parsingHeader(char* uri,char* host,char* port,char* filename){
    char *p;

    if ((p = strchr(uri,'/'))) {
        *p = '\0';
        sscanf(p+2, "%s", host);
    }
    else {
        strcpy(host, uri);
    }

    if ((p = strchr(host,':'))) {
        *p = '\0';
        sscanf(host, "%s", host);
        sscanf(p+1, "%s", port);
    }

    if ((p = strchr(port, '/'))) {
        *p = '\0';
        sscanf(port, "%s", port);
        sscanf(p+1, "%s", filename);
    }
}

void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE); // 텍스트 줄을 rp에서 읽고 buf에 복사한 후 널 문자로 종료시킨다. 최대 MAXLINE - 1개의 바이트를 읽는다.
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) { // 첫번째 매개변수(buf)와 두번째 매개변수('\r\n')가 같을 경우 0을 반환
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

void make_request_to_server(int ptsfd,char* url, char* host, char* port, char* method, char* version, char* filename) {
    char buf[MAXLINE];

    if (strlen(filename) == 0) {
        strcpy(url, "/\n");
    }else{
        strcpy(url, "/");
        strcat(url, filename);
    }
    sprintf(buf, "%s %s %s\r\n", method, url, version);
    sprintf(buf, "%sHost: %s:%s\r\n", buf, host, port);
    sprintf(buf, "%s%s", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);
    Rio_writen(ptsfd, buf, strlen(buf));
    printf("%s", buf);
}
/*
GET http://13.125.102.254:9999 HTTP/1.0
Host:13.125.102.254:5000
User-Agent: "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3"
Connection: keep
Proxy-Connection: keep
*/