#include <stdio.h>
#include "csapp.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// define function
void doit(int fd);
void parse_uri(char *uri, char *hostname, char *filename);
void proxying(int client_fd, char *hostname, char *port, char *filename);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

void test_conn(int fd);


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";



int main(int argc, char **argv) {
  printf("%s", user_agent_hdr);
  int listenfd, clientfd, servfd;  // 클라이언트와 연결할 listen소켓과 client연결 소켓, 서버에 연결할 소켓
  // 클라이언트와 서버 연결 정보 저장 변수
  char client_host[MAXLINE], client_port[MAXLINE], serv_host[MAXLINE];
  // 프록시에서 접속해야하는 서버는 알고 있다라는 가정을 한다.
  socklen_t clientlen;

  struct sockaddr_storage clientaddr;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 클라이언트 소켓 생성
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    clientfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, client_host, MAXLINE, client_port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", client_host, client_port);
    // 클라언트가 요구하는 uri를 확인
    doit(clientfd);   // line:netp:tiny:doit
    // test_conn(clientfd);
    Close(clientfd);  // line:netp:tiny:close
  }
  // return 0;
}

void doit(int fd) { 
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], filename[MAXLINE];   // 저장할 버퍼와 들어온 http method version url을 저장할 초기 변수 선언
  char port[10] = "5001";
  char hostname[MAXLINE];

  int serverfd;

  rio_t rio;

  // 클라이언트 요청 확인
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);  // 버퍼에서 method uri version을 받는다.
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");   //tiny서버에서는 get method만 처리한다. 이외의 요청이 오면 기각할 수 있게 한다.
    return;
  }
  
  //헤더를 파싱해서 uri을 얻어오기
  // printf("uri: %s\n\n",uri);
  printf("buffer : %s\n",buf);
  parse_uri(uri,hostname, filename);

  
  // printf("%s",hostname);
  // strcpy(hostname,"localhost");
  // strcpy(filename,"/cgi-bin/adder?123&123");
  // 파싱한 코드를 재조합 해서 tiny 서버로 접속해야함.
  proxying(fd, hostname, port, filename);

}
void test_conn(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Received: %s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    // Tiny 서버로 요청 전달
    int tiny_fd = Open_clientfd("129.154.49.89", "5001");
    sprintf(buf, "%s / HTTP/1.0\r\n\r\n", method);
    Rio_writen(tiny_fd, buf, strlen(buf));

    // Tiny 서버의 응답을 클라이언트에게 전달
    Rio_readinitb(&rio, tiny_fd);
    while (Rio_readlineb(&rio, buf, MAXLINE) > 0) {
        Rio_writen(fd, buf, strlen(buf));
    }

    Close(tiny_fd);
}
/* 
  파싱을 해서 얻어와야 하는것
  접속하려는 uri는 그대로 가야함
  하지만 포트번호는 변경이 됨으로 포트 전까지 파싱을 해야한다.
 */
void parse_uri(char *uri, char *hostname, char *filename){  // 여러가지 값을 반환해야 하기 때문에 포인터 변수를 이용
  // uri를 파싱해서 port와 뒤의 디렉터리를 분리해야함.
  // 이를 통해 서버로 url과 port를 붙여서 접속을 하고
  // 원하는 파일디렉터리를 붙여서 요청한다.
  char *hoststart;
  char *hostend;

  char *pathstart;
  // http:// 헤더가 있을경우 url로 넘어갈 수 있게 포인터 위치를 변경해준다.
  if (strstr(uri, "://")) {
      hoststart = strstr(uri, "://") + 3;
  } else {
      hoststart = uri;
  }
  hostend = strchr(hoststart,':');
  if (hostend == NULL){
    strcpy(hostname, hoststart);
  }
  else {
    strncpy(hostname,hoststart,hostend-hoststart);
    hostname[hostend-hoststart] = '\0';
  }
  printf("")

  pathstart = strchr(hoststart, '/');
  if (pathstart) {
      *pathstart = '\0';
      strcpy(hostname, hoststart);
      *pathstart = '/';
      strcpy(filename, pathstart);
  } else {
      strcpy(hostname, hoststart);
      strcpy(filename, "/");
  }
  printf("%s\n",hostname);
}

// 서버에 연결 후 클라이언트가 요청한 url을 서버에게 전송 그 응답값을 리턴
void proxying(int client_fd, char *hostname, char *port, char *filename){
  int server_fd;
  char buf[MAXLINE], request[MAXLINE];
  rio_t server_rio, client_rio;

  printf("proxing host:%s\n",hostname);
  server_fd = Open_clientfd(hostname, port);

  sprintf(request, "GET /%s HTTP/1.0\r\n", filename);
  sprintf(request, "%sHost: %s\r\n", request, hostname);
  sprintf(request, "%s\r\n", request);
  
  // 요청 전송
  Rio_writen(server_fd, request, strlen(request));

  // 응답 읽기 및 처리
  Rio_readinitb(&server_rio, server_fd);
  Rio_readinitb(&client_rio, client_fd);
  while(Rio_readlineb(&server_rio, buf, MAXLINE)>0){
    Rio_writen(client_fd, buf, strlen(buf));
    printf("%s",buf);
  }
  Close(server_fd);

}



/* 에러처리를 위한 함수 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXLINE];

  /* HTTP 응답 바디 */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* HTTP응답 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Cntent-Length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}