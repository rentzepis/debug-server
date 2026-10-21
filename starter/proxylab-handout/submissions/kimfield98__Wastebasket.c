#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// 함수 선언부
void doit(int fd);
void parse_uri(char *uri,char *hostname,char *path, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void blank_response(int fd, char *buf);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char ** argv) {
  
  int listenfd, *connfdp;  // 소켓 디스크립터와 클라이언트 소켓 디스크립터 포인터를 선언
  socklen_t clientlen;  // 클라이언트 주소의 길이를 저장하는 변수
  struct sockaddr_storage clientaddr;  // 클라이언트 주소 정보를 저장하는 구조체
  char port[MAXLINE], hostname[MAXLINE];  // 호스트명과 포트 정보를 저장할 변수
  pthread_t tid;  // pthread 라이브러리를 사용하여 쓰레드 ID를 저장할 변수

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);  // 프로그램을 종료하고 오류 메시지 출력
  }

  // 지정한 포트로 소켓 생성 및 바인딩
  listenfd = Open_listenfd(argv[1]);
  
  // 무한 루프: 클라이언트의 연결을 지속적으로 수락하기 위함
  while (1) {
    clientlen = sizeof(clientaddr);  // 클라이언트 주소 길이를 초기화
    connfdp = Malloc(sizeof(int));  // 클라이언트 소켓 디스크립터를 저장할 메모리를 동적으로 할당
    *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);  // 클라이언트 연결을 수락하고 소켓 디스크립터를 저장
    Pthread_create(&tid, NULL, thread, connfdp);  // 클라이언트 연결을 처리하는 쓰레드를 생성
  }
  return 0;
}

// 쓰레드에서 실행되는 함수 (클라이언트 연결을 처리)
void * thread(void *vargp){
  // void 포인터를 int 포인터로 형변환하고 connfd를 얻음
  int connfp = *((int *)vargp);
  // 쓰레드를 detach 상태로 만듦
  // 쓰레드가 종료될 때 자원을 자동으로 회수하도록 하기 위함
  Pthread_detach(pthread_self());
  // 클라이언트 소켓 디스크립터를 저장한 메모리를 해제
  Free(vargp);
  // 클라이언트 요청을 처리하는 함수를 호출
  doit(connfp);
  // 클라이언트와의 연결을 종료
  Close(connfp);
  // 쓰레드 함수의 반환 값은 NULL로 설정
  return NULL;
}

void doit(int fd)
{
  int clientfd;

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], host[MAXLINE];
  char header[MAXLINE];
  char response_body[MAXLINE];
  char object[MAX_OBJECT_SIZE];
  char path[MAXLINE], port[MAXLINE];
  rio_t rio, rio2;

  // 클라이언트로부터의 요청을 읽고 파싱
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);

  // 지원하지 않는 HTTP 메서드인 경우 501 Not Implemented 에러를 응답
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // URI를 해석하여 호스트, 파일 경로, 포트를 추출
  parse_uri(uri, host, filename, port);

  // HTTP 요청 헤더를 생성
  sprintf(header, "GET %s HTTP/1.0\r\n", filename);
  sprintf(header, "%sUser-Agent: %s", header, user_agent_hdr);
  sprintf(header, "%sConnection: close\r\n", header);
  sprintf(header, "%sProxy-Connection: close\r\n", header);

  // 불필요한 헤더를 걸러내고 나머지 헤더를 요청 헤더에 추가
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(&rio, buf, MAXLINE);

    if (strstr(buf, "User-Agent:")) {
      continue;
    }
    else if (strstr(buf, "Connection:")) {
      continue;
    }
    else if (strstr(buf, "Proxy-Connection:")) {
      continue;
    }
    else {
      sprintf(header, "%s%s", header, buf);
    }
  }

  // 요청 헤더를 출력
  printf("Request-header:\n");
  printf("%s", header);

  // 원격 서버에 연결하고 요청을 전달
  clientfd = Open_clientfd(host, port);
  Rio_readinitb(&rio2, clientfd);
  Rio_writen(clientfd, header, strlen(header));

  // 원격 서버로부터의 응답 헤더를 받아 클라이언트에게 전달
  Rio_readlineb(&rio2, buf, MAXLINE);
  sprintf(header, buf);

  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(&rio2, buf, MAXLINE);
    sprintf(header, "%s%s", header, buf);
  }

  printf("response-header:\n");
  printf("%s", header);
  Rio_writen(fd, header, strlen(header));

  // 원격 서버로부터의 응답 본문을 받아 클라이언트에게 전달
  size_t n;
  while ((n = Rio_readlineb(&rio2, buf, MAXLINE)) != 0) {
    printf("%d바이트 받음\n", n);
    printf("%s\n", buf);
    Rio_writen(fd, buf, n);
  }

  // 클라이언트와의 연결을 종료
  Close(clientfd);
}


// 클라이언트에게 에러 응답을 보내는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // HTML 형식의 에러 본문 생성
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // HTTP 응답 헤더 생성 및 클라이언트에게 전송
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // HTML 에러 본문을 클라이언트에게 전송
  Rio_writen(fd, body, strlen(body));
}

// 빈 응답을 클라이언트에게 보내는 함수
void blank_response(int fd, char *buf)
{
  // HTTP 응답 헤더 생성 및 클라이언트에게 전송
  strcpy(buf, "HTTP/1.0 200 OK\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));
  return;
}


// URI를 파싱하여 호스트명, 파일 경로, 포트를 추출하는 함수
void parse_uri(char *uri,char *hostname,char *filename,char *port)
{
  // URI에서 "//"을 찾아 p1에 포인터 저장
  char *p1 = strstr(uri, "//");
  // p1을 "//" 이후로 이동
  p1 += 2;

  // p1 이후에서 다음 "/"을 찾아 p2에 포인터 저장
  char *p2 = strstr(p1, "/");

  // p1 이후에서 다음 ":"을 찾아 p3에 포인터 저장
  char *p3 = strstr(p1, ":");  
  
  // p3가 NULL이 아니면 (포트 정보가 있는 경우)
  if (p3 != NULL) {

    // p3 다음부터 p2까지를 포트로 복사
    strncpy(port, p3 + 1, p2 - p3 - 1);
    // p1부터 p3 이전까지를 호스트명으로 복사
    strncpy(hostname, p1, p3 - p1);
  }
  // 포트 정보가 없는 경우
  else {
    // 기본 포트인 80을 설정
    strcpy(port, "80");
    // p1부터 p2 이전까지를 호스트명으로 복사
    strncpy(hostname, p1, p2 - p1);
  }
  // p2부터 끝까지를 파일 경로로 복사
  strcpy(filename, p2);
}