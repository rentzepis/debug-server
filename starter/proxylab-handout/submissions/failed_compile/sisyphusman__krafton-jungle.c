#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// http://localhost:8000/home.html

// Proxy 서버에서 클라이언트 요청의 URI를 파싱하는 함수
static int parse_uri_proxy(const char *uri, char *host, char *port, char *path)
{
  char hostport[MAXLINE]; // host:port 임시 저장
  size_t len;

  if (strncasecmp(uri, "http://", 7) != 0)
  {
    return -1;
  }

  const char *p = uri + 7;                      // p - "http://" 다음 문자 위치
  const char *slash = strchr(p, '/');           // p에서부터 '/' 위치 찾기, 없으면 문자열 끝까지

  if (!slash)                                   // slash가 NULL
  {       
    slash = p + strlen(p);                      // slash가 문자열 끝을 가리키도록 설정, strchr(p, '/') → "/home.html"
  }       

  len = slash - p;                              // host:port 부분의 길이 계산
  if (len >= sizeof(hostport))                  // hostport 버퍼 크기를 초과하면 에러
  {       
    return -1;        
  }       

  memcpy(hostport, p, len);                     // p가 가리키는 문자열에서 len 바이트만 복사
  hostport[len] = '\0';                         // 마지막에 널 문자('\0') 추가
                                                // hostport = "example.com:8000\0"

  char *colon = strchr(hostport, ':');          // host:port 구분하는 ':' 위치, ':' 있는 경우 → host와 port 분리
  if (colon)        
  {       
    *colon = '\0';                              // ':' 자리에 널 문자 넣어 host 문자열 종료
    strcpy(host, hostport);       
    strcpy(port, colon + 1);        
  }       
  else        
  {       
    strcpy(host, hostport);                     // ':' 없으면 포트는 기본값 80
    strcpy(port, "80");       
  }       

if (*slash == '\0')                             // '/' 가 없으면 path는 "/"로, 있으면 그 이후 부분이 path
  {
    strcpy(path, "/");
  }
  else
  {
    strcpy(path, slash);
  }

  return 0;
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);                 // 포인터가 가리키는 값을 꺼내서 connfd에 저장
  free(vargp);                                  // int 값을 가져온 뒤, 필요없어서 free       
  pthread_detach(pthread_self());               // 현재 스레드 자신을 detached 상태로 만듬 - 스레드-per-connection 모델

  doit(connfd);                                 // 클라이언트 요청을 읽고, 원서버로 전달하고, 응답을 받아서 클라이언트에게 돌려주는 역할
  Close(connfd);                                // 해당 클라이언트 소켓 닫기
  return NULL;
}
 
// 클라이언트 요청을 읽고, 원서버로 전달하고, 응답을 받아서 클라이언트에게 돌려주는 역할
void doit(int clientfd)
{
  rio_t crio;                                   // crio는 rio_t 타입 구조체(버퍼, fd, 포인터 상태 저장)
  char line[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

  Rio_readinitb(&crio, clientfd);               // CS:APP에서 제공하는 Robust I/O 초기화 함수
  if (!Rio_readlineb(&crio, line, MAXLINE))     // 클라이언트로부터 한 줄 읽어오기 시도 crio 구조체 안에 등록된 소켓(clientfd)에서 데이터를 읽어옴
  {
    return;
  }

  printf("Request: %s", line);
  sscanf(line, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))                // method가 "GET"이 아니라면 블록 실행
  {
    clienterror(clientfd, method, "501", "Not Implemented",
                "Proxy supports only GET/HEAD");
    return;
  }

  // 절대 경로 URI만 지원
  char host[MAXLINE], port[16], path[MAXLINE];
  if (parse_uri_proxy(uri, host, port, path) < 0)
  {
    clienterror(clientfd, uri, "400", "Bad Request",
                "Absolute-form URI required");
    return;
  }

  // 나머지 클라이언트 헤더는 읽어서 버린다(빈 줄까지)
  while (1)
  {
    if (!Rio_readlineb(&crio, line, sizeof(line)))      // 클라이언트 소켓에서 한 줄을 안전하게 읽어와 line 버퍼에 저장하고 C 문자열로 만들어주는 함수
    {
      return;
    }
    if (!strcmp(line, "\r\n"))                          // 같으면 0, 헤더의 끝을 만나면 루프 종료
    {
      break;
    }
  }

  int serverfd = Open_clientfd(host, port);             // 원서버 연결
  if (serverfd < 0)
  {
    clienterror(clientfd, host, "502", "Bad Gateway", "Cannot connect to origin");
    return;
  }

  // 요청라인/헤더를 아주 간단히 재작성해서 전송
  char out[MAXBUF];
  int n = 0;
  n += snprintf(out + n, sizeof(out) - n, "GET %s HTTP/1.0\r\n", path);  // printf처럼 서식을 적용해 문자열을 만든 다음, 그 결과를 버퍼 str에 최대 size-1 글자만 복사하고, 마지막에 항상 '\0'을 붙임
  n += snprintf(out + n, sizeof(out) - n, "Host: %s", host);

  if (strcmp(port, "80"))
  {
    n += snprintf(out + n, sizeof(out) - n, ":%s", port);
  }

  n += snprintf(out + n, sizeof(out) - n, "\r\n");
  n += snprintf(out + n, sizeof(out) - n, "%s", user_agent_hdr);
  n += snprintf(out + n, sizeof(out) - n, "Connection: close\r\n");
  n += snprintf(out + n, sizeof(out) - n, "Proxy-Connection: close\r\n");
  n += snprintf(out + n, sizeof(out) - n, "\r\n");
  Rio_writen(serverfd, out, n);

  // 응답을 그대로 릴레이
  rio_t srio;
  Rio_readinitb(&srio, serverfd);                                   // 버퍼링된 안전한 I/O 함수를 쓸 수 있게 해 줌
  char buf[MAXBUF];
  ssize_t r;

  while ((r = Rio_readnb(&srio, buf, sizeof(buf))) > 0)             // 서버에서 받은 응답을 읽어서, 그대로 클라이언트에게 전달(relay) 하는 루프
  {
    printf("%s", buf);
    Rio_writen(clientfd, buf, r);                                   // 읽은 그대로 클라이언트로 보냄
  }

  Close(serverfd);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, // 에러 발생 시 HTML 에러 페이지 전송
                 char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");                 // 에러 메시지 body 구성
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);             // HTTP 응답 전송
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// 메인 스레드: Open_listenfd(port) → 무한루프 Accept() → 새 커넥션마다 스레드 생성
int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) // 포트 번호 확인
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);                                                  // 지정된 포트 번호로 서버 소켓을 열고, 클라이언트 연결을 수락할 준비가 된 리스닝 소켓 디스크립터를 반환하는 함수

  while (1)
  {
    clientlen = sizeof(clientaddr);
    printf("실행중...\n");
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                         // 클라이언트 연결을 수락(blocking)
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);   // 클라이언트 주소 구조체(clientaddr)를 사람이 읽을 수 있는 형태로 변환

    printf("Accepted connection from (%s, %s)\n", hostname, port);

    int *connfdp = malloc(sizeof(int));                                               // 새로 얻은 연결 소켓 번호 connfd를 스레드 함수로 넘기기 위해
    *connfdp = connfd;                                                                // 그 공간에 connfd 값을 복사

    pthread_t tid;                                                                    // 스레드 ID를 저장할 변수
    pthread_create(&tid, NULL, thread, connfdp);                                      // connfdp를 인자로 넘겨서 thread() 함수를 실행하는 새 스레드를 하나 만든다
  }

  printf("%s", user_agent_hdr);
  return 0;
}

// telnet localhost 4500
// GET http://localhost:8000/home.html HTTP/1.0
// Host: localhost:8000