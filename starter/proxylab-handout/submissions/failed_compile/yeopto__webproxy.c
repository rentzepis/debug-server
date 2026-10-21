#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key= "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";

void doit(int connfd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port, rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);
void *thread(void *vargsp);

// functions for caching
void init_cache(void); // 캐시 초기화
int reader(int connfd, char *url);
void writer(char *url, char *buf);


// 캐시를 저장할 하나하나의 블럭
typedef struct {
  char *url; // url 담을 변수
  int *flag; // 캐시가 비어있는지 차 있는지 구분할 변수
  int *cnt; // 최근 방문 순서 나타내기 위한 변수
  int *content; // 클라이언트에 보낼 내용 담겨있는 변수 
} Cache_info;

Cache_info *cache; // cache 변수 선언
int readcnt; // 세마포어 cnt 할 변수
sem_t mutex, w;

int main(int argc, char **argv) {

  if(argc != 2) {
    fprintf(stderr, "usage :%s <port> \n", argv[0]);
    exit(1);
  }

  init_cache();
  int listenfd, connfd;
  socklen_t clientlen;
  char hostname[MAXLINE], port[MAXLINE];
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  listenfd = Open_listenfd(argv[1]);

  while(1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    // 연결 수락 메세지 출력
    Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s %s).\n",hostname, port);

    Pthread_create(&tid, NULL, thread, (void *)connfd);
  }
  return 0;
}

void *thread(void *vargs) {
  int connfd = (int)vargs;
  Pthread_detach(pthread_self()); // 분리해주면 알아서 할 일 다하고 종료함 스레드는
  doit(connfd);
  Close(connfd);
}

void doit(int connfd) {
  int end_serverfd; // 엔드서버(tiny.c) fd

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];
  
  // 요청 인자들
  char hostname[MAXLINE], path[MAXLINE];
  int port;

  rio_t rio, server_rio; /*rio is client's rio,server_rio is endserver's rio*/

  char url[MAXLINE];
  char content_buf[MAX_OBJECT_SIZE]; // cache에 쓸 내용을 담은 버퍼

  /* 클라이언트가 보낸 요청 헤더에서 method, uri, version을 가져옴.*/
  /* GET http://localhost:8000/home.html HTTP/1.1 */
  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version); /*read the client request line*/
  strcpy(url, uri);

  if (strcasecmp(method, "GET")) { // 같으면 0을 반환함 즉, 다르다면 출력해라.
    printf("Proxy does not implement the method");
    return;
  }
  
  // 캐시에서 찾았을 때 캐시 적중 
  if (reader(connfd, url)) {
    return ;
  }
  /*parse the uri to get hostname,file path ,port*/
  /* 프록시 서버가 엔드 서버로 보낼 정보들을 파싱함. */
  // hostname -> localhost, path -> /home.html, port -> 8000
  parse_uri(uri, hostname, path, &port);

  /*build the http header which will send to the end server*/
  /* 프록시 서버가 엔드 서버로 보낼 요청 헤더들을 만듦. endserver_http_header가 채워진다. */
  build_http_header(endserver_http_header, hostname, path, port, &rio);

  /*connect to the end server*/
  /* 프록시 서버와 엔드 서버를 연결함 */
  end_serverfd = connect_endServer(hostname, port, endserver_http_header); // 클라이언트 fd 생성하는 것
  // clinetfd connected from proxy to end server at proxy side
  // port: 8000
  if (end_serverfd < 0) {
    printf("connection failed\n");
    return;
  }

  /* 엔드 서버에 HTTP 요청 헤더를 보냄 */
  Rio_readinitb(&server_rio, end_serverfd);
  /*write the http header to endserver*/
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));

  /* 엔드 서버로부터 응답 메세지를 받아 클라이언트에 보내줌. */
  /*receive message from end server and send to the client*/
  size_t n;
  int total_size = 0; // 캐시에 담을 바이트 수
  while((n = Rio_readlineb(&server_rio,buf, MAXLINE)) != 0) {
    printf("proxy received %d bytes,then send\n",n);
    Rio_writen(connfd, buf, n); // connfd -> client와 proxy 연결 소켓. proxy 관점.

    if (total_size + n < MAX_OBJECT_SIZE) {
      strcpy(content_buf + total_size, buf);
    }
    total_size += n;
  }
  // 캐시 컨텐트의 최대 크기를 넘지 않았다면 캐시에 쓰기
  if (total_size < MAX_OBJECT_SIZE) {
    writer(url, content_buf);
  }

  Close(end_serverfd);
}

void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio) {
  char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];

  /* 응답 라인 만들기 */
  sprintf(request_hdr, requestlint_hdr_format, path);

  /* 클라이언트 요청 헤더들에서 Host header와 나머지 header들을 구분해서 넣어줌 */
  /*get other request header for client rio and change it */
  while(Rio_readlineb(client_rio, buf, MAXLINE)>0) {
    if (strcmp(buf, endof_hdr) == 0) break; /* EOF, '\r\n' 만나면 끝 */

    /* 호스트 헤더 찾기 */
    if (!strncasecmp(buf, host_key, strlen(host_key))) { /*Host:*/ //일치하는 게 있으면 0
        strcpy(host_hdr, buf);
        continue;
    }
    /* 나머지 헤더 찾기 */
    if (strncasecmp(buf, connection_key, strlen(connection_key))
          && strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
          && strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
      strcat(other_hdr,buf);
    }
  }
  if (strlen(host_hdr) == 0) {
      sprintf(host_hdr,host_hdr_format,hostname);
  }

  /* 프록시 서버가 엔드 서버로 보낼 요청 헤더 작성 */
  sprintf(http_header,"%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, other_hdr, endof_hdr);
  
  return ;
}

// 프록시 서버와 엔드 서버 연결
inline int connect_endServer(char *hostname,int port,char *http_header) {
  char portStr[100];
  sprintf(portStr,"%d",port);
  return Open_clientfd(hostname,portStr);
}

void parse_uri(char *uri,char *hostname,char *path,int *port) {
  *port = 80; // default port
  char* pos = strstr(uri,"//");  /* http://이후의 string들 */

  pos = pos!=NULL? pos+2:uri;  /* http:// 없어도 가능 */ 

  /* port와 path를 파싱 */
  char *pos2 = strstr(pos,":");
  if (pos2!=NULL) {
    *pos2 = '\0';
    sscanf(pos,"%s",hostname);
    sscanf(pos2+1,"%d%s",port,path); // port change from 80 to client-specifying port
  } else {
    pos2 = strstr(pos,"/");
    if (pos2!=NULL) {
      *pos2 = '\0';
      sscanf(pos,"%s",hostname);
      *pos2 = '/';
      sscanf(pos2,"%s",path);
    } else {
      sscanf(pos,"%s",hostname);
    }
  }
  return;
}

/* cache 초기화 */
void init_cache() {
  Sem_init(&mutex, 0, 1);
  Sem_init(&w, 0, 1);
  readcnt = 0;
  cache = (Cache_info *)Malloc(sizeof(Cache_info) * 10);                  /* 캐시의 최대 크기는 1MB이고 캐시의 객체는 최대 크기가 100KB이라서 10개의 공간을 잡음 */
  for (int i = 0; i < 10; i++) {
      cache[i].url = (char *)Malloc(sizeof(char) * 256);                  /* url 공간을 256바이트 할당 */
      cache[i].flag = (int *)Malloc(sizeof(int));                         /* flag 공간을 4바이트 할당 */
      cache[i].cnt = (int *)Malloc(sizeof(int));                          /* cnt 공간을 4바이트 할당 */
      cache[i].content = (char *)Malloc(sizeof(char) * MAX_OBJECT_SIZE);  /* content의 공간을 100KB 할당 */
      *(cache[i].flag) = 0;                                               /* flag 0으로 설정, 즉, 비어있다는 뜻 */
      *(cache[i].cnt) = 1;                                                /* cnt 0으로 설정, 최근에 찾은 것일 수록 0이랑 가까움 */
  }
}

/* cache에서 요청한 url 있는지 찾기 */
/* 세마포어를 이용해서 reader가 먼저 되고 여러 thread가 읽고 있으면 writer는 할 수가 없게 한다. */
int reader(int connfd, char *url) {
  int return_flag = 0;    /* 캐시에서 찾았으면 1, 못찾으면 0 */
  P(&mutex);              
  readcnt++;
  if(readcnt == 1) {
    P(&w);
  }
  V(&mutex);

  /* cache를 다 돌면서 cache에 써있고 cache의 url과 현재 요청한 url이 같으면 client fd에 cache의 내용 쓰고 해당 cache의 cnt를 0으로 초기화 후 break */
  for(int i = 0; i < 10; i++) {
    if(*(cache[i].flag) == 1 && !strcmp(cache[i].url, url)) {
      Rio_writen(connfd, cache[i].content, MAX_OBJECT_SIZE);
      return_flag = 1;
      *(cache[i].cnt) = 1;
      break;
    }
  }    
    
  /* 모든 cache객체의 cnt를 하나씩 올려줌, 즉, 방문안한 일수를 올려줌 */
  for(int i = 0; i < 10; i++) {
    (*(cache[i].cnt))++;
  }

  P(&mutex);
  readcnt--;
  if(readcnt == 0) {
    V(&w);
  }
  V(&mutex);
  return return_flag;
}

/* cache에서 요청한 url의 정보 쓰기 */
/* 세마포어를 이용해서 writer는 한번 */
void writer(char *url, char *buf) {
  P(&w);

  int idx = 0;                        /* 작성할 곳을 가리키는 index */
  int max_cnt = 0;                    /* 가장 오래 방문 안한 일수 */

  /* 10개의 cache를 돌고 만약 비어있는 곳이 있으면 비어있는 곳에 index를 찾고, 없으면 가장 오래 방문 안한 곳의 index 찾기 */
  for(int i = 0; i < 10; i++) {
    if(*(cache[i].flag) == 0) {
      idx = i;
      break;
    }
    if(*(cache[i].cnt) > max_cnt) {
      idx = i;
      max_cnt = *(cache[i].cnt);
    }
  }
  /* 해당 index에 cache 작성 */
  *(cache[idx].flag) = 1;
  strcpy(cache[idx].url, url);
  strcpy(cache[idx].content, buf);
  *(cache[idx].cnt) = 1;

  V(&w);
}