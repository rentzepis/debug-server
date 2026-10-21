
#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n"; //

void doit(int fd);
void parse_uri(char *uri, char *host, char *port, char *path);
void read_requesthdrs(rio_t *rp);
void forward_request_headers(rio_t *client_rio, int server_fd, char *host);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);

int main(int argc, char **argv) {
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));  // 반드시 malloc 사용!
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
}

void doit(int fd)
{
  int server_fd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], path[MAXLINE], port[MAXLINE];
  rio_t client_rio, server_rio;

  Rio_readinitb(&client_rio, fd);
  Rio_readlineb(&client_rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not Implemented", "Tiny Proxy does not implement this method");
    return;
  }

  parse_uri(uri, host, port, path);

  server_fd = Open_clientfd(host, port);
  if (server_fd < 0)
  {
    clienterror(fd, host, "404", "Not Found", "Tiny Proxy couldn't connect to the server");
    return;
  }

  Rio_readinitb(&server_rio, server_fd);

  snprintf(buf, sizeof(buf), "GET %s HTTP/1.0\r\n", path);
  Rio_writen(server_fd, buf, strlen(buf));

  forward_request_headers(&client_rio, server_fd, host);

  // Forward response from server to client
  size_t n;
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
  {
    Rio_writen(fd, buf, n);
  }

  Close(server_fd);
}

void parse_uri(char *uri, char *host, char *port, char *path)
{
  char *hostbegin;
  char *hostend;
  char *pathbegin;
  int len;

  if (strncasecmp(uri, "http://", 7) != 0)
  {
    host[0] = '\0';
    port[0] = '\0';
    path[0] = '\0';
    return;
  }

  hostbegin = uri + 7;
  hostend = strpbrk(hostbegin, ":/");

  if (hostend == NULL)
  {
    strcpy(host, hostbegin);
    port[0] = '\0';
    path[0] = '\0';
    return;
  }

  len = hostend - hostbegin;
  strncpy(host, hostbegin, len);
  host[len] = '\0';

  if (*hostend == ':')
  {
    char *portbegin = hostend + 1;
    char *portend = strpbrk(portbegin, "/");
    if (portend != NULL)
    {
      len = portend - portbegin;
      strncpy(port, portbegin, len);
      port[len] = '\0';
      pathbegin = portend;
    }
    else
    {
      strcpy(port, portbegin);
      path[0] = '\0';
      return;
    }
  }
  else
  {
    strcpy(port, "80");
    pathbegin = hostend;
  }

  if (*pathbegin == '/')
  {
    strcpy(path, pathbegin);
  }
  else
  {
    path[0] = '\0';
  }
}

void read_requesthdrs(rio_t *rp) // 요청 헤더 읽기 함수
{
  // rp : RIO 버퍼 구조체 포인터
  char buf[MAXLINE]; // 버퍼 선언

  Rio_readlineb(rp, buf, MAXLINE); // 첫 번째 헤더 라인 읽기
  printf("%s", buf); // 헤더 라인 출력
  while (strcmp(buf, "\r\n")) // 빈 줄이 나올 때까지 반복
  {
    Rio_readlineb(rp, buf, MAXLINE); // 다음 헤더 라인 읽기
    printf("%s", buf); // 헤더 라인 출력
  }
  return;
}

void forward_request_headers(rio_t *client_rio, int server_fd, char *host)
{
  char buf[MAXLINE];
  int host_header_found = 0;

  // Forward headers from client to server
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
  {
    if (strcmp(buf, "\r\n") == 0)
      break; // End of headers

    if (strncasecmp(buf, "Host:", 5) == 0)
    {
      host_header_found = 1;
      Rio_writen(server_fd, buf, strlen(buf));
    }
    else if (strncasecmp(buf, "Connection:", 11) == 0 ||
             strncasecmp(buf, "Proxy-Connection:", 17) == 0 ||
             strncasecmp(buf, "User-Agent:", 11) == 0)
    {
      // Skip these headers
      continue;
    }
    else
    {
      Rio_writen(server_fd, buf, strlen(buf));
    }
  }

  if (!host_header_found)
  {
    sprintf(buf, "Host: %s\r\n", host);
    Rio_writen(server_fd, buf, strlen(buf));
  }

  // Add required headers
  Rio_writen(server_fd, "Connection: close\r\n", strlen("Connection: close\r\n"));
  Rio_writen(server_fd, "Proxy-Connection: close\r\n", strlen("Proxy-Connection: close\r\n"));
  Rio_writen(server_fd, (void *)user_agent_hdr, strlen(user_agent_hdr));
  Rio_writen(server_fd, "\r\n", strlen("\r\n"));
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) // 클라이언트 에러 응답 함수
{
  // fd : 클라이언트 연결 디스크립터
  // cause : 에러 원인
  // errnum : 에러 번호
  // shortmsg : 짧은 에러 메시지
  // longmsg : 긴 에러 메시지
  char buf[MAXLINE], body[MAXBUF]; // 버퍼 선언
  int n = 0; // 버퍼 길이

  n = snprintf(body, sizeof(body), "<html><title>Tiny Error</title>"); // HTML 본문 시작
  n += snprintf(body + n, sizeof(body) - n, "<body bgcolor=\"ffffff\">\r\n"); // 배경색 설정
  n += snprintf(body + n, sizeof(body) - n, "%s: %s\r\n", errnum, shortmsg); // 에러 번호와 짧은 메시지
  n += snprintf(body + n, sizeof(body) - n, "<p>%s: %s</p>\r\n", longmsg, cause); // 긴 메시지와 원인
  n += snprintf(body + n, sizeof(body) - n, "<hr><em>The Tiny Web server</em>\r\n"); // 푸터

  size_t bodylen = (n < 0 || n >= (int)sizeof(body)) ? strlen("<html><body>Internal Error</body></html>")
                                                     : (size_t)n; // 본문 길이 계산

  if (n < 0 || n >= (int)sizeof(body)) // 본문 작성 실패 시 대체 본문 작성
  {
    strcpy(body, "<html><body>Internal Error</body></html>"); // 대체 본문
  }

  n = snprintf(buf, sizeof(buf), "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // 응답 라인 작성
  n += snprintf(buf + n, sizeof(buf) - n, "Content-type: text/html\r\n"); // 콘텐츠 타입 헤더 작성
  n += snprintf(buf + n, sizeof(buf) - n, "Content-length: %zu\r\n\r\n", bodylen); // 콘텐츠 길이 헤더 작성

  if (n < 0 || n >= (int)sizeof(buf)) // 버퍼 오버플로우 검사
  {
    char fallback[] =
        "HTTP/1.0 500 Internal Server Error\r\n"
        "Connection: close\r\n\r\n"; // 대체 응답 작성
    Rio_writen(fd, fallback, strlen(fallback)); // 대체 응답 전송
    return;
  }

  Rio_writen(fd, buf, n); // 응답 헤더 전송
  Rio_writen(fd, body, bodylen); // 응답 본문 전송
}


void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}
