#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void goOrigin(int fd)
{
  int clientfd;
  char *host, *port, buf[MAXLINE], secondbuf[MAXLINE];

w
  rio_t rio;
  rio_t test;
  int qq;

  
  
  
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);

  // 진짜 서버에 보내기
  clientfd = Open_clientfd(host, port);
  Rio_writen(clientfd, buf, strlen(buf));




  //서버에서 보낸거 클라이언트에 주기
  Rio_readinitb(&test, clientfd);
  //TCP는 한번에 다 받지 않기 때문에 계속 버퍼가 빌때 까지 받아야 함
  while((qq = Rio_readlineb(&test, secondbuf, MAXLINE)) != 0)
  {
    Rio_writen(fd, secondbuf, qq);
  }
  Close(clientfd);
}

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);                                                        // 듣기 소켓 오픈

  // 반복적으로 연결요청을 접수
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept     // 연결 요청 접수
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    goOrigin(connfd);   // line:netp:tiny:doit                                                  // 트랜잭션 수행
    Close(connfd);  // line:netp:tiny:close
  }
}
