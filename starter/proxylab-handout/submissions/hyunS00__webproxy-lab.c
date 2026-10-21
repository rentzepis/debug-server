#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void do_proxy(int connfd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_header(rio_t *rp, char *header);
void parse_http_url(char *url, char *host, char *port, char *uri);
void read_requestheader(rio_t *rp, char *header, char *host, char *port);

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

  listenfd = open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    do_proxy(connfd);
    Close(connfd);
  }
  return 0;
}

void do_proxy(int connfd)
{
  int clientfd;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE], header[MAXLINE], uri[MAXLINE];
  char host[MAXLINE], port[MAXLINE];
  rio_t conn_rio, client_rio;
  ssize_t n;
  rio_readinitb(&conn_rio, connfd);
  rio_readlineb(&conn_rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s",buf);
  sscanf(buf, "%s %s %s", method, url, version);

  if(strcasecmp(method, "GET")){
    clienterror(connfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  parse_http_url(url, host, port, uri);

  read_requestheader(&conn_rio, header, host, port);

  printf("host:%s port:%s uri:%s\n", host, port, uri);

  if ((clientfd = open_clientfd(host, port)) < 0){
    printf("error clientfd: %d\n",clientfd);
    clienterror(connfd, url, "404", "hah", "test");
    return;
  }
  // 요청헤더
  printf("clientfd: %d\n",clientfd);
  strcpy(buf, "GET ");
  strcat(buf, uri);
  strcat(buf, " HTTP/1.0\r\n");

  // 헤더 보내기
  rio_readinitb(&client_rio, clientfd);
  rio_writen(clientfd, buf, strlen(buf));
  rio_writen(clientfd, header, strlen(header));

  // 헤더 받기
  read_header(&client_rio, header);
  rio_readnb(&client_rio, buf, MAXBUF);
  printf("response header:\n%s\n",header);
  rio_writen(connfd, header, strlen(header));

  rio_readnb(&client_rio, buf, MAXBUF);
  printf("response body:\n%s\n",buf);
  rio_writen(connfd, buf, strlen(buf));
  close(clientfd);

}

void read_header(rio_t *rp, char *header){
  char buf[MAXLINE];
  strcpy(header,"");
  do {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    strcat(header, buf);
  } while (strcmp(buf, "\r\n"));
  return;
}

void clienterror(int fd, char *cause, char *errnum, char *shormsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shormsg);
  sprintf(body, "%s<p>%s: %s\r\n",body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HHTTP/1.0 %s %s\r\n", errnum, shormsg);
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
  printf("%s",buf);
  printf("%s",body);
}

void parse_http_url(char *url, char *host, char *port, char *uri) {
  // "http://" 이후부터 검색하여 호스트 및 포트 추출
  const char *start = strstr(url, "//");
  if (start != NULL) {
      start += 2; // "http://" 다음 문자부터 시작

      // ':' 문자를 찾아 호스트와 포트 추출
      const char *colon = strchr(start, ':');
      if (colon != NULL) {
          strncpy(host, start, colon - start);
          host[colon - start] = '\0';

          // '/' 문자를 찾아 포트와 URI 추출
          const char *slash = strchr(colon + 1, '/');
          if (slash != NULL) {
              strncpy(port, colon + 1, slash - colon - 1);
              port[slash - colon - 1] = '\0';
              strcpy(uri, slash);
          } else {
              strcpy(port, colon + 1);
              strcpy(uri, "/");
          }
      } else {
          // ':' 문자가 없는 경우 포트를 기본값인 "80"으로 설정
          strcpy(port, "80");

          // '/' 문자를 찾아 호스트와 URI 추출
          const char *slash = strchr(start, '/');
          if (slash != NULL) {
              strncpy(host, start, slash - start);
              host[slash - start] = '\0';
              strcpy(uri, slash);
          } else {
              strcpy(host, start);
              strcpy(uri, "/");
          }
      }
  }
}

void read_requestheader(rio_t *rp, char *header, char *host, char *port){
  int include_host = 0;

  char buf[MAXLINE];
  strcpy(header,"");
  rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    if(strstr(buf, "User-Agent") || strstr(buf, "Connection") || strstr(buf, "Proxy-Connection") || !strcmp(buf, "\r\n")){
      continue;
    }

    if(strstr(buf, "Host") != NULL){
      include_host = 1;
    }
    strcat(header, buf);
  };
  if(!include_host){
    strcat(header, "Host: ");
    strcat(header, host);
    strcat(header, ":");
    strcat(header, port);
    strcat(header, "\r\n");
  }

  strcat(header, "Connection: Close\r\n");
  strcat(header, "Proxy-Connection: Close\r\n");
  strcat(header, "User-Agent: ");
  strcat(header, user_agent_hdr);
  strcat(header, "\r\n");

  return;
}