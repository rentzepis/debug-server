#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

//------------
#include "csapp.h"

void proxy(int clientfd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize, char *method);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs, char *method);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, clientfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 클라이언트 소켓 생성
  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    // 클라로부터의 연결을 수락하는 소켓 파일 디스크립터
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    // 클라이언트 요청 처리
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    ;
    proxy(clientfd);
    Close(clientfd);
  }
}

// 클라로부터 받은 요청을 처리
void proxy(int clientfd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], path[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE];
  char full_url[MAXLINE];
  rio_t rio_client, rio_server;

  // Proxy 헤더 읽기
  Rio_readinitb(&rio_client, clientfd);
  Rio_readlineb(&rio_client, buf, MAXLINE); // 여서 한 줄 읽음 (ex: GET /index.html HTTP/1.1)
  sscanf(buf, "%s %s %s", method, uri, version);
  printf("::: %s %s %s :::\n", method, uri, version);

  if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0)
  {
    clienterror(clientfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // uri 로부터 HOSTNAME, PATH, PORT 추출
  const char *urlStart = strstr(uri, "://");
  if (urlStart)
  {
    urlStart += 3;
  }
  else
  {
    urlStart = uri;
  }
  // 포트가 있을 경우와 없을 경우의 처리
  const char *portStart = strchr(urlStart, ':'); // ':'를 찾아 포트 시작 지점 탐색
  const char *pathStart = strchr(urlStart, '/'); // '/'를 찾아 경로 시작 지점 탐색

  if (portStart && (!pathStart || portStart < pathStart))
  {
    strncpy(hostname, urlStart, portStart - urlStart); // strcpy에서 복사할 개수 추가
    hostname[portStart - urlStart] = '\0';             // 마지막에 문자열 종료
    sscanf(portStart + 1, "%[^/]", port);              // ':' 뒤에서 '/' 앞까지가 포트
  }
  else
  {
    if (pathStart)
    {
      strncpy(hostname, urlStart, pathStart - urlStart); // '/' 앞까지 추출
      hostname[portStart - urlStart] = '\0';             // 문자열 종료
    }
    else
    {
      strcpy(hostname, urlStart); // 경로가 없으면 전체
    }
    strcpy(port, "80");
  }

  if (pathStart)
  {
    strcpy(path, pathStart); // '/' 뒤 경로
  }
  else
  {
    strcpy(path, "/"); // 경로 없으면 기본 경로 '/'
  }

  printf("Connecting to hostname: %s, port: %s, path: %s\n", hostname, port, path);
  int serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0)
  {
    printf("서버와의 연결 실페: %s %s\n", hostname, port);
    return;
  }

  // serverfd 초기화
  Rio_readinitb(&rio_server, serverfd);

  // 클라이언트의 요청을 서버에 전달
  sprintf(buf, "%s %s %s\r\n", method, path, version); // 새로운 요청 라인 구성 //TODO
  Rio_writen(serverfd, buf, strlen(buf));              // 요청 라인 전달

  // 추가 헤더 전달
  sprintf(buf, "Host: %s\r\n", hostname);
  Rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "%s", user_agent_hdr);
  Rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "Proxy-Connection: closer\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  // 나머지 헤더 전달
  while (Rio_readlineb(&rio_client, buf, MAXLINE) > 0)
  {
    if (strcmp(buf, "\r\n") == 0)
      break;                                // 헤더 끝
    Rio_writen(serverfd, buf, strlen(buf)); // 헤더 전달
  }
  Rio_writen(serverfd, "\r\n", 2); // 빈 줄로 헤더 종료

  // 서버로부터 응답을받아 클라이언트로 전달
  ssize_t n;
  while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0)
  {
    Rio_writen(clientfd, buf, n); // 서버 응답을 클라이언트로 전달
  }
  Close(serverfd); // 서버 연결 닫기
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));

  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  Rio_writen(fd, body, strlen(body));
}

int parse_uri(char *uri, char *filename, char *cgiargs)
{
  printf("parse_uri함수호출\n");
  char *ptr;

  if (!strstr(uri, "cgi-bin"))
  {
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri) - 1] == '/')
      strcat(filename, "home.html");
    return 1;
  }
  else
  {
    ptr = index(uri, '?');
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0';
    }
    else
      strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}
