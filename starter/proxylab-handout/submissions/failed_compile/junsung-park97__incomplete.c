#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit (int fd);
void read_requesthdrs(rio_t* rp, char* host_header, char* other_header);
void parse_uri (char* uri, char* hostname, char* port, char* path);
void reasseble (char* req, char* path, char* hostname, char* other_header);
void forward_response (int servedf, int fd);
void clienterror (int fd, char* cause, char* errnum, char* shortmsg, char* longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

//클라이언트의 HTTP 요청을 받아서 적절한 응답을 제공
void doit (int fd) {

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  char host_header[MAXLINE], other_header[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char request_buf[MAXLINE];
  rio_t rio;

  // 요청라인과 헤더들을 읽어라.
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }


  read_requesthdrs(&rio, host_header, other_header);
  parse_uri(uri, hostname, port, path);
  int servedf = Open_clientfd(hostname, port);
  reasseble(request_buf, path, hostname, other_header);
  Rio_writen(servedf, request_buf, strlen(request_buf));
  forward_response(servedf, fd);
  Close(servedf);
}

// 이게 헤더 분리하는 함수인가? 근데 왜 분리를 안함?
void read_requesthdrs(rio_t* rp, char* host_header, char* other_header) {
  char buf[MAXLINE];

  // 이걸 왜 해야돼? 이해가 안가네,,
  host_header[0] = '\0';
  other_header[0] = '\0';

  // 왜 host_header는 cpy이고 other_header는 cat이야? 어짜피 이 함수의 목적은 헤더 분리인데?
  while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n")) {
    if (!strncasecmp(buf, "Host:", 5)) {
      strcpy(host_header, buf);
    }
    else if (!strncasecmp(buf, "User-Agent", 11) || !strncasecmp(buf, "Connection", 11) || !strncasecmp(buf, "Proxy-Connection", 17)) {
      continue; // 무시
    }
    else {
      // 이렇게 하면 else if에 해당되는 데이터도 카피되는거 아니야?
      strcat(other_header, buf);
    }
  }
}

// 요청헤더의 uri 파싱
void parse_uri (char* uri, char* hostname, char* port, char* path) {
  char *hostbegin, *hostend, *portbegin, *pathbegin;
  char buf[MAXLINE];

  strcpy(buf, uri);
  // hostbegin = //의 첫번째 위치의 포인터를 반환
  hostbegin = strstr(buf, "//");
  // hostbegin은 //을 기준으로 +2 에서 시작
  hostbegin = (hostbegin != NULL) ? hostbegin + 2 : buf; 

  pathbegin = strchr(hostbegin, "/");
  if (pathbegin != NULL) {
    strcpy(path, pathbegin);
    // hostname 부분을 잘라내기 위해 NULL종결자를 한다고?
    *pathbegin = '\0'; 
  }
  else {
    strcpy(path, "/");
  }

  portbegin = strchr(hostbegin, ":");
  if (portbegin != NULL) {
    *portbegin = '\0';
    strcpy(hostname, hostbegin);
    // :을 기준으로 +1부터 port가 시작
    strcpy(port, portbegin + 1); 
  }
  else {
    strcpy(hostname, hostbegin);
    strcpy(port, "80"); // 포트번호가 명시되지 않으면 "80"
  }
}

// 지금까지 요청 헤더에서 분리했던 host헤더와 other헤더 그 외에 데이터를 새로운 요청 헤더로 재조합
void reasseble (char* req, char* path, char* hostname, char* other_header) {
  sprintf(req,
    "GET %s HTTP/1.0\r\n"
    "Host: %s\r\n"
    "%s"
    "Connection: close\r\n"
    "Proxy-Connection: close\r\n"
    "%s"
    "\r\n",
    path,
    hostname,
    user_agent_hdr,
    other_header
  );
}

// 목적지 서버로 부터 반환 받은 데이터(응답)를 클라이언트에게 전송
void forward_response (int servedf, int fd) {
  // servedf ?? -> doit()
  rio_t serve_rio;
  char response_buf[MAXBUF];

  Rio_readinitb(&serve_rio, servedf);

  ssize_t n;
  while ((n = Rio_readnb(&serve_rio, response_buf, MAXBUF)) > 0) {
    Rio_writen(fd, response_buf, n);
  }
}

// 서버에서 client요청으로 처리하다가 error가 났을때 상황 전파.
void clienterror (int fd, char* cause, char* errnum, char* shortmsg, char* longmsg) {
  char buf[MAXLINE], body[MAXLINE];

  // HTTP응답 본문 구성
  // 왜 이렇게 쓸데없이 만들어놨지?
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s : %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  //HTTP응답 출력
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Context type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Context Length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

// strcpy(dest, src); src에서 dest로 문자열 복사 단, 덮어쓰기 안됨
// strcat(dest, src); strcpy와 같지만 덮어쓰기됨
// strcmp() : 두 문자열이 같으면 0을 반환하는 함수 (C내장함수)
// strstr(uri, "cgi-bin") : uri에서 cgi-bin라는 문자열의 첫번째 위치의 포인터를 반환.
// strcasecmp() :

// char *index(const char *s, int c); 문자열 s에서 문자 c를 처음발견한 위치의 포인터를 반환
// 쿼리 문자열 "?" 
// S_ISREG() : 시스템 내장함수 -> 파일 타입을 확인하는 매크로 일반파일 : 1, 아니면 : 0
// 관습적으로 동적 콘텐츠를 실행하는 프로그램들은 cgi-bin이라는 폴더안에 넣어둠.
// Dup2() ->
// Execve() ->
// setenv() ->
// Wait() -> 
// 표준 헤더 규격?