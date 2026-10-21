// telnet 143.248.225.88 3000
// GET http://143.248.225.88:5000/ HTTP/1.1

#include <stdio.h>
#include "queue.c"
#include "sbuf.h"
// #include "sbuf.c"
#define NTHREADS 4
#define SBUFSIZE 16

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

#include "csapp.h"
#include <stdlib.h>

void doit(int fd);
void send_request(char *uri, int fd);
void *thread(void *vargp);
char dest[MAXLINE];

// 요청들을 저장할 큐 선언.
Queue queue;

// 세마포어 변수 선언
sbuf_t sbuf;
sem_t mutex;


int main(int argc, char **argv) 
{
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  
  // 요청을 저장하는 큐 초기화
  InitQueue(&queue);
  // 세마포어 초기화
  sem_init(&mutex,0,1);
  sbuf_init(&sbuf, SBUFSIZE);

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 요청을 기다린다.
  listenfd = Open_listenfd(argv[1]);
  
  // 스레드 풀을 생성.
  // NTHREADS 개의 스레드를 미리 생성해준다.
  int i = 0;
  for (i = 0; i < NTHREADS; i++) /* Create worker threads */
  {
    Pthread_create(&tid, NULL, thread, NULL);
  }

  // 요청을 받으면 미리 만들어놨던 스레드에게 요청 전달
  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    sbuf_insert(&sbuf, connfdp); /* Insert connfd in buffer */
  }

}

// 스레드 생성
void *thread(void *vargp)
{
  Pthread_detach(pthread_self());
  while (1) {
    /* Remove connfd from buffer */
    int connfd = sbuf_remove(&sbuf);
    printf("<-----------------start of proxy server request------------------>\n");
    /* Service client */
    // 해당 스레드에서 요청 받은 작업을 수행한다.
    doit(connfd);
    Close(connfd);
  }
  
  printf("<-----------------end of request------------------>\n");
  return NULL;
}

// proxy 서버로 요청을 전달한다.
void send_request(char *uri, int fd){

  int clientfd;
  char buf[MAXLINE], proxy_res[MAX_OBJECT_SIZE], tmp[MAX_OBJECT_SIZE], tmp2[MAX_OBJECT_SIZE], port[MAX_OBJECT_SIZE], new_uri[MAX_OBJECT_SIZE];
  rio_t rio;
  char *p, *q;

  // uri 파싱 진행.
  // ex) http://143.248.225.88:5000/home.html
  sscanf(strstr(uri, "http://"), "http://%s", tmp);
  // tmp = 143.248.225.88:5000/home.html
  if((p = strchr(tmp, ':')) != NULL){
    *p = '\0';
    // dest = 143.248.225.88
    // tmp2 = 5000/home.html
    sscanf(tmp, "%s", dest);
    sscanf(p+1, "%s", tmp2);

    q = strchr(tmp2, '/');
    *q = '\0';
    // 5000  home.html
    sscanf(tmp2, "%s", port);
    // port = 5000
    
    *q = '/';
    sscanf(q, "%s", new_uri);
    // new_uri = /home.html
    
  }

  // proxy <-> tiny connection
  clientfd = Open_clientfd(dest, port);

  // request header를 만들고
  Rio_readinitb(&rio, clientfd);
  sprintf(buf, "GET %s HTTP/1.0\r\n\r\n", new_uri);

  // 만든 헤더를 proxy <-> tiny 연결 버퍼에 넣어준다.
  Rio_writen(clientfd, buf, strlen(buf));
  printf("%s", buf);

  // tiny 서버에서 들어온 response를 읽어준다.
  Rio_readnb(&rio, proxy_res, MAX_OBJECT_SIZE);

  // response를 telnet <-> proxy 연결 버퍼에 전달한다.
  Rio_writen(fd, proxy_res, MAX_OBJECT_SIZE);

  // proxy <-> tiny 연결을 끊어준다.
  Close(clientfd);


  // 큐를 안전하게 이용하기 위한 세마포어.
  P(&mutex);
  // 큐에 노드가 9개 이상 들어있다면 enqueue를 해주기 전에 dequeue를 해준다.
  if (queue.count > 10){
    Dequeue(&queue);
    printf("------------------dequeue %d\n", queue.count);
  }

  // printf("%s\n", (proxy_res));

  // queue에 요청을 저장해준다.
  Enqueue(&queue, uri, &proxy_res);
  // 큐를 모두 이용했으므로 잠금을 풀어준다.
  V(&mutex);

  printf("----------------enqueue %d\n", queue.count);
  
}

// fd: telnet <-> proxy
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);

  printf("Request line:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // 큐 조회 전 잠금 설정
  P(&mutex);
  // 큐의 앞에서 부터 옆 노드로 가면서
  Node *cache = queue.front;
  while (cache != NULL){
    // 새로 받은 요청과 같은 uri가 큐에 있다면
    if(strcmp(cache->request_line, uri) == 0){
      printf("--------------------cache hit!!\n");
      Rio_writen(fd, cache->response, strlen(cache->response));
      // tiny 서버로 요청을 보내지 않고 바로 return한다.
      return;
    }
    cache = cache->next;
  }
  // 큐 조회 후 잠금 풀기.
  V(&mutex);

  // 이전에 받은 적 없는 요청을 받았으므로
  // tiny 서버로 요청을 보낸다.
  send_request(&uri, fd);


}
