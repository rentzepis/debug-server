#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"
#include "queue.c"
#include "sbuf.h"

/* 생성하는 쓰레드 갯수 */
#define NTHREADS 4
#define SBUFSIZE 16

/* 권장 최대 캐시 및 객체 크기 */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* 함수 선언 */
void doit(int fd);
void send_request(char *uri, int fd);
void *thread(void *vargp);

/* 전역 변수 선언 */
char dest[MAXLINE];
Queue queue;

/* 전역 변수 보호하는 세마포어 변수 */
sbuf_t sbuf;
sem_t mutex;


int main(int argc, char **argv){
  int i, listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* 큐 초기화 */
  InitQueue(&queue);
  /* mutex 초기화 */
  sem_init(&mutex,0,1);

  /* 명령줄 args 확인 */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* 받을 준비가 된 listenfd(파일 디스크립터) 생성 */
  listenfd = Open_listenfd(argv[1]);
  /* 쓰레드 풀 동시성 접근 동기화 위한 sbuf 초기화 */
  sbuf_init(&sbuf, SBUFSIZE);
  for (i = 0; i < NTHREADS; i++){ /* 쓰레드 풀 생성 */
    Pthread_create(&tid, NULL, thread, NULL);
  }

  while (1) {
    clientlen = sizeof(clientaddr);
    /* 연결 가능 상태(listenfd) 소켓에 들어온 클라이언트의 연결 요청 승낙 후 연결한 소켓 식별자 */
    connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    sbuf_insert(&sbuf, connfdp); /* sbuf버퍼에 connfd 삽입 */
  }
}

/*
 * thread - 쓰레드 풀에서 쓰레드 꺼내 연결 및 사용 후 반납
 */
void *thread(void *vargp)
{
  Pthread_detach(pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);
    Close(connfd);
  }
  return NULL;
}

/*
 * send_request - proxy server가 client가 요청한 server에 요청 전달
 */
void send_request(char *uri, int fd){
  int clientfd;
  char buf[MAXLINE], proxy_res[MAX_OBJECT_SIZE], tmp[MAX_OBJECT_SIZE], tmp2[MAX_OBJECT_SIZE], port[MAX_OBJECT_SIZE], new_uri[MAX_OBJECT_SIZE];
  rio_t rio;
  char *p, *q;

  /* 전달 받은 uri 파싱 */
  sscanf(strstr(uri, "http://"), "http://%s", tmp);
  if((p = strchr(tmp, ':')) != NULL){
    *p = '\0';
    sscanf(tmp, "%s", dest);
    sscanf(p+1, "%s", tmp2);

    q = strchr(tmp2, '/');
    *q = '\0';
    sscanf(tmp2, "%s", port);
    *q = '/';
    sscanf(q, "%s", new_uri);
  }

  /* client에서 요청한 server와 proxy server 연결 식별자 */
  clientfd = Open_clientfd(dest, port);

  /* server의 요청 헤더 생성 및 전송 */
  Rio_readinitb(&rio, clientfd);
  sprintf(buf, "GET %s HTTP/1.0\r\n\r\n", new_uri);
  Rio_writen(clientfd, buf, strlen(buf));       /* buf에서 식별자 clientfd로 buf 사이즈만큼 전송 */

  Rio_readnb(&rio, proxy_res, MAX_OBJECT_SIZE); /* rio 파일에서 텍스트 읽고, 메모리 위치 proxy_res로 복사 */

  Rio_writen(fd, proxy_res, MAX_OBJECT_SIZE);   /* proxy_res에서 식별자 fd로 MAX_OBJECT_SIZE 만큼 전송 */
  Close(clientfd);

  /* mutex로 전역변수인 queue(cache list) 동시 접근 보호 */
  P(&mutex);
  /* queue(cache list)가 꽉차면 FIFO로 제일 처음 cache 삭제 */
  if (queue.count > 10){
    Dequeue(&queue);
  }
  /* queue(cache list)에 cache 정보 삽입 */
  Enqueue(&queue, uri, &proxy_res);
  V(&mutex);
}

/*
 * doit - client와 proxy server의 HTTP 트랜잭션 처리 
 */
void doit(int fd){
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* 요청 라인 및 헤더 읽기 */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);

  printf("Request line:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  /* mutex로 전역변수인 queue(cache list) 동시 접근 보호 */
  P(&mutex);
  Node *cache = queue.front;
  /* 캐시가 존재할 때 */
  while (cache != NULL){
    /* 요청받은 uri가 캐시에 있으면 cache에 저장한 response 값 return */
    if(strcmp(cache->request_line, uri) == 0){
      Rio_writen(fd, cache->response, strlen(cache->response));
      return;
    }
    cache = cache->next;
  }
  V(&mutex);
  /* 요청받은 uri의 정보가 캐시에 없으면 server에 요청해서 정보 받아오기 */
  send_request(&uri, fd);
}