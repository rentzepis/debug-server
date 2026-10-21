#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int connfd);
void parse_uri(char *uri, char *host, char *port, char *path);
void RequestToServer(char connfd, char *method, char *host, char *path);
void ResponseToClient(int connfd, int pClientFd);

void doit(int connfd)
{
  int pClientFd;
  char buf[MAXLINE], host[MAXLINE], port[MAXLINE], path[MAXLINE], uri[MAXLINE], method[MAXLINE], version[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers to proxy:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  parse_uri(uri, host, port, path);

  pClientFd = Open_clientfd(host, port);                    // proxy <-> server

  RequestToServer(pClientFd, method, host, path);
  ResponseToClient(connfd, pClientFd);
  Close(pClientFd);
}

void parse_uri(char *uri, char *host, char *port, char *path)
{
  char *ptr;

  // ex) http://www.naver.com/home.html

  if((ptr = strstr(uri, "://")))        
  {
    ptr += 3;
    strcpy(host, ptr);                    // host = www.naver.com/home.html
  }

  if((ptr = strchr(host, ':')))
  {
    *ptr = "\0";                          // host = www.naver.com
    ptr += 1;
    strcpy(port, ptr);                    // port = 80/home.html (port가 지정됐다면)
  }
  else
  {
    if((ptr = strchr(host, '/')))
    {
      *ptr = "\0";                        // ptr = home.html
      ptr += 1;
    }
    strcpy(port, "80");                   // port = 80
  }

  if((ptr = strchr(port, '/')))
  {
    *ptr = "\0";                          // port = 80
    ptr += 1;
    strcpy(path, "/");
    strcat(path, ptr);                    // path = /home.html
  }
  else
    strcpy(path, "/");
    strcat(path, ptr);                    // path = /home.html
}

void RequestToServer(char connfd, char *method, char *host, char *path)
{
  char buf[MAXLINE];
  printf("Request headers to server: ");
  printf("%s %s %s\n", method, path, "HTTP/1.0");

  // request headers
  sprintf(buf, "GET %s %s\r\n", path, "HTTP/1.0");
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnections: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);

  Rio_writen(connfd, buf, (size_t)strlen(buf));
}

void ResponseToClient(int connfd, int pClientFd)
{
  char buf[MAX_CACHE_SIZE];
  ssize_t n;
  rio_t rio;

  Rio_readinitb(&rio, pClientFd);
  n = Rio_readnb(&rio, buf, MAX_CACHE_SIZE);
  Rio_writen(pClientFd, buf, n);
}

int main(int argc, char **argv)
{
  int listenFd, pConnFd;
  char hostName[MAXLINE], port[MAXLINE];
  socklen_t clientLen;
  struct sockaddr_storage clientAddr;

  if(argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenFd = Open_listenfd(argv[1]);                      // open proxy listen socket
  while(1)
  {
    clientLen = sizeof(struct sockaddr_storage);
    pConnFd = Accept(listenFd, &clientAddr, &clientLen);
    
    Getnameinfo((SA *)&clientAddr, &clientLen, hostName, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostName, port);

    doit(pConnFd);
    Close(pConnFd);
  }

  return 0;
}