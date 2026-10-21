#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void forward_request(int serverfd, const char *hostname, const char *path)
{
  char buf[MAXLINE];

  /* request-line */
  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  Rio_writen(serverfd, buf, strlen(buf));

  /* Host 헤더 */
  sprintf(buf, "Host: %s\r\n", hostname);
  Rio_writen(serverfd, buf, strlen(buf));

  /* 프록시 전용 헤더 */
  Rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));
  Rio_writen(serverfd, "Connection: close\r\n", strlen("Connection: close\r\n"));
  Rio_wirten(serverfd, "Proxy-Connection: close\r\n\r\n", strlen("Proxy-Connection: close\r\n\r\n"));
}

void forward_response(int serverfd, int clientfd)
{
  rio_t rio;
  char buf[MAXLINE];
  size_t n;

  Rio_readinitb(&rio, serverfd);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
  {
    Rio_writen(clientfd, buf, n);
  }
}

int main(int argc, char **argv)
{
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  listenfd = Open_listenfd(argv[1]);

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    handle_client(connfd);
    Close(connfd);
  }

  return 0;
}

void handle_client(int connfd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio;

  /* 클라이언트 요청 읽고 파싱 */
  Rio_readinitb(&rio, connfd);
  if (Rio_readlineb(&rio, buf, MAXLINE) <= 0)
    return;

  sscanf(buf, "%s %s %s", method, uri, version);

  parse_uri(uri, hostname, port, path);

  int serverfd = Open_clientfd(hostname, port);

  forward_request(serverfd, hostname, path);

  forward_response(serverfd, connfd);

  Close(serverfd);
}

int parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *hostbegin, *hostend, *pathbegin;
  int len;

  /* http:// 접두사가 있으면 건너뛰고, 없으면 uri 그대로 사용 */
  if ((hostbegin = strstr(uri, "http://") != NULL))
  {
    hostbegin += strlen("http://");
  }
  else
  {
    hostbegin = uri;
  }

  /* 호스트 끝과 경로 시작 위치 찾기 */
  hostend = strpbrk(hostbegin, ":/");
  if (hostend == NULL)
  {
    strcpy(hostname, hostbegin);
    strcpy(port, "80");
    strcpy(path, "/");
    return 0;
  }

  /* 호스트명 복사 */
  len = hostend - hostbegin;
  strncpy(hostname, hostbegin, len);
  hostname[len] = '\0';

  /* 포트가 명시되었는지 확인 */
  if (*hostend == ':')
  {
    char *portbegin = hostend + 1;
    char *portend = strpbrk(portbegin, "/");
    if (portend)
    {
      len = portend - portbegin;
      strncpy(port, portbegin, len);
      port[len] = '\0';
    }
    else
    {
      strcpy(port, portbegin);
    }
  }
  else
  {
    strcpy(port, "80");
    pathbegin = hostend;
  }

  /* 경로 복사 */
  if (pathbegin)
  {
    strcpy(path, pathbegin);
  }
  else
  {
    strcpy(path, "/");
  }

  return 0;
}
