#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* prototypes */
void doit(int fd);
void *thread(void *vargp);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, char *pathname, char *port);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv) {
  int listenfd, *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  // 실행 시 인수로 포트번호가 들어오지 않았을 경우 exit
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 서버 소켓 열기
  listenfd = Open_listenfd(argv[1]);

  while (1) {
    clientlen = sizeof(clientaddr);
    // connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // 쓰레드에서 요청 읽기 및 처리
    Pthread_create(&tid, NULL, thread, connfd);
  }
}

/*
 * thread - 쓰레드를 분리해서 클라이언트의 요청을 처리하고, 완료 후 연결을 닫음
 */
void *thread(void *vargp) {
  int connfd = *((int *)vargp);
  // 쓰레드 분리
  Pthread_detach((pthread_self()));
  // 동적할당 해제
  Free(vargp);
  // 클라이언트 요청 처리
  doit(connfd);
  // 연결 닫기
  Close(connfd);
  return NULL;
}

/*
 * doit - 한 개의 HTTP transaction 을 처리
 */
void doit(int fd) {
  signal(SIGPIPE, SIG_IGN);
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  // uri parsing 을 위한 변수 선언
  char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
  rio_t rio;

  /* request, header 값 읽기 */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  // 여기서 요청을 읽고 HTTP 메소드, URI, HTTP 버전을 파싱
  sscanf(buf, "%s %s %s", method, uri, version);
  printf(":: %s %s %s ::\n", method, uri, version);

  // 입력된 uri 파싱
  if (parse_uri(uri, hostname, pathname, port) < 0) {
    clienterror(fd, uri, "400", "Bad Request", "Failed to parse URI");
    return;
  }

  // GET 메소드 이외의 메소드가 들어왔을 경우 error
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }

  read_requesthdrs(&rio);
  forward_request(fd, hostname, pathname, port);
}

/*
 * parse_uri - URI 파싱
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port) {
  char *hostbegin, *hostend, *pathbegin;
  int len;

  // URI가 http:// 로 시작하지 않으면 오류 반환
  if (strncasecmp(uri, "http://", 7) != 0) {
    return -1;
  }

  // 호스트명 추출
  hostbegin = uri + 7;  // "http://" 이후부터 호스트명 시작
  hostend = strpbrk(hostbegin, " :/\r\n\0");  // 호스트명 끝 탐색
  len = hostend - hostbegin;
  strncpy(hostname, hostbegin, len);
  hostname[len] = '\0';

  // 포트 번호 추출
  strcpy(port, "80");  // 기본 포트는 80

  // 포트 번호가 있으면 추출
  if (*hostend == ':') {
    char *portbegin = hostend + 1;
    char *portend = strpbrk(portbegin, "/\r\n\0");  // '/' 또는 줄바꿈이 나오기 전까지가 포트 번호
    len = portend - portbegin;
    strncpy(port, portbegin, len);  // 포트 번호를 복사
    port[len] = '\0';
  }

  // 경로 추출
  pathbegin = strchr(hostbegin, '/');
  if (pathbegin == NULL) {
    strcpy(pathname, "/");  // 경로가 없으면 기본 경로로 "/"
  } else {
    strcpy(pathname, pathbegin);  // 경로가 있으면 그대로 복사
  }

  printf("parse arguments :: uri: %s, hostname: %s, pathname: %s, port: %s\n", uri, hostname, pathname, port);
  return 0;
}

/*
 * read_requesthdrs - HTTP request headers 를 읽고 파싱
 */
void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
 * forward_request - 웹서버로 요청 보내기
 */
void forward_request(int clientfd, char *hostname, char *pathname, char *port) {
  int serverfd;
  char buf[MAXLINE], response[MAXBUF];
  rio_t rio;

  // 원격 서버에 연결 - 클라이언트 소켓 열기
  serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0) {
    printf("Failed to connect to server.\n");
    return;
  }

  // 요청 헤더 작성 및 전달
  sprintf(buf, "GET %s HTTP/1.0\r\n", pathname);
  Rio_writen(serverfd, buf, strlen(buf));

  // 필수 헤더 추가
  // Host
  sprintf(buf, "Host: %s:%s\r\n", hostname, port);
  Rio_writen(serverfd, buf, strlen(buf));

  // User-Agent
  sprintf(buf, user_agent_hdr);
  Rio_writen(serverfd, buf, strlen(buf));

  // connection
  sprintf(buf, "Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  // Proxy-Connection
  sprintf(buf, "Proxy-Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  sprintf(buf, "\r\n");  // 마지막 헤더를 추가한 후 공백 줄을 넣어 요청 종료를 표시
  Rio_writen(serverfd, buf, strlen(buf));

  // 원격 서버 응답을 클라이언트로 전달
  Rio_readinitb(&rio, serverfd);
  size_t n;
  while ((n = Rio_readlineb(&rio, response, MAXBUF)) != 0) {
    Rio_writen(clientfd, response, n);
  }
  Close(serverfd);
}


/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=" "ffffff" ">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}