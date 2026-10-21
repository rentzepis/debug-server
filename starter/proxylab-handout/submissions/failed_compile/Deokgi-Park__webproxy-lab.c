#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NTHREADS 30
#define SBUFSIZE 120

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
void do_it(int fd);
int parse_uri(char *hostname, char *port, char *path, char *uri);
void *thread(void *vargp);
static char cachebuf[MAX_CACHE_SIZE];

int main(int argc, char **argv)
{
  char hostname[MAXLINE], port[MAXLINE];
  // 듣기 소켓, 연결 소켓 생성(fd)
  int listenfd, *connfdp;
  // 클라이언트 소켓 길이 설정
  socklen_t clientlen;
  // 클라이언트 소켓 저장소 설정
  struct sockaddr_storage clientaddr;
  // 쓰래드
  pthread_t tid;
  /* 포트 인자 존재 여부 체크 */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 내 소켓 찾고 듣기 소켓으로 설정
  listenfd = Open_listenfd(argv[1]);

  while (1)
  {
    // 클라이언트 주소 크기를 스토리지 사이즈만큼의 크기로 설정
    clientlen = sizeof(clientaddr);
    // 연결 소켓 값을 받은 준비
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    // 받은 소켓 정보 셋팅
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    // 받은 소켓 정보 확인
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}
void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  do_it(connfd);
  Close(connfd);
}

void do_it(int connfd){
  int clientfd;
  char buf[MAXLINE],  host[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char uri_ptos[MAXLINE];
  rio_t rio;

  /* Read request line and headers from Client */
  Rio_readinitb(&rio, connfd);                      // rio 버퍼와 fd(proxy의 connfd)를 연결시켜준다. 
  Rio_readlineb(&rio, buf, MAXLINE);                  // 그리고 rio(==proxy의 connfd)에 있는 한 줄(응답 라인)을 모두 buf로 옮긴다. 
  printf("Request headers to proxy:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);      // buf에서 문자열 3개를 읽어와 각각 method, uri, version이라는 문자열에 저장 

  /* Parse URI from GET request */
  // if (!(parse_uri(uri, uri_ptos, host, port)))
  //   return -1;
  parse_uri(uri, uri_ptos, host, port);

  clientfd = Open_clientfd(host, port);
  if(clientfd < 0){
    Rio_writen(connfd, cachebuf, MAX_CACHE_SIZE);  
    Close(clientfd);
    return;
  }             // clientfd = proxy의 clientfd (연결됨)
  do_request(clientfd, method, uri_ptos, host);     // clientfd에 Request headers 저장과 동시에 server의 connfd에 쓰여짐
  do_response(connfd, clientfd);
          
  Close(clientfd);                                  // clientfd 역할 끝
}

/* do_request: proxy => server */
void do_request(int clientfd, char *method, char *uri_ptos, char *host){
  char buf[MAXLINE];
  printf("Request headers to server: \n");     
  printf("%s %s %s\n", method, uri_ptos, "HTTP/1.0");

  /* Read request headers */        
  sprintf(buf, "GET %s %s\r\n", uri_ptos, "HTTP/1.0");     // GET /index.html HTTP/1.0
  sprintf(buf, "%sHost: %s\r\n", buf, host);                // Host: www.google.com     
  sprintf(buf, "%s%s", buf, user_agent_hdr);                // User-Agent: ~(bla bla)
  sprintf(buf, "%sConnections: close\r\n", buf);            // Connections: close
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);   // Proxy-Connection: close

  /* Rio_writen: buf에서 clientfd로 strlen(buf) 바이트로 전송*/
  Rio_writen(clientfd, buf, (size_t)strlen(buf)); // => 적어주는 행위 자체가 요청하는거야~@!@!
}

/* do_response: server => proxy */
void do_response(int connfd, int clientfd){
  char buf[MAX_CACHE_SIZE];
  ssize_t n;
  rio_t rio;

  Rio_readinitb(&rio, clientfd);  
  n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE);
  strcpy(cachebuf, buf);
  Rio_writen(connfd, buf, n);
}

/* parse_uri: Parse URI, Proxy에서 Server로의 GET request를 하기 위해 필요 */
int parse_uri(char *uri, char *uri_ptos, char *host, char *port){ 
  char *ptr;

  /* 필요없는 http:// 부분 잘라서 host 추출 */
  if (!(ptr = strstr(uri, "://"))) 
    return -1;                        // ://가 없으면 unvalid uri 
  ptr += 3;                       
  strcpy(host, ptr);                  // host = www.google.com:80/index.html

  /* uri_ptos(proxy => server로 보낼 uri) 추출 */
  if((ptr = strchr(host, '/'))){  
    *ptr = '\0';                      // host = www.google.com:80
    ptr += 1;
    strcpy(uri_ptos, "/");            // uri_ptos = /
    strcat(uri_ptos, ptr);            // uri_ptos = /index.html
  }
  else strcpy(uri_ptos, "/");

  /* port 추출 */
  if ((ptr = strchr(host, ':'))){     // host = www.google.com:80
    *ptr = '\0';                      // host = www.google.com
    ptr += 1;     
    strcpy(port, ptr);                // port = 80
  }  
  else strcpy(port, "80");            // port가 없을 경우 "80"을 넣어줌

  /* 
  Before Parsing (Client로부터 받은 Request Line)
  => GET http://www.google.com:80/index.html HTTP/1.1

  Result Parsing (순차적으로 host, uri_ptos, port으로 파싱됨)
  => host = www.google.com
  => uri_ptos = /index.html
  => port = 80

  After Parsing (Server로 보낼 Request Line)
  => GET /index.html HTTP/11. 
  */ 

  return 0; // function int return => for valid check
}
