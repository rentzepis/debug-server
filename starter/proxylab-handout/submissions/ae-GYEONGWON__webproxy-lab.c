#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *requestline_format = "GET %s HTTP/1.0\r\n";
static const char *user_agent_header = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *host_header_format = "Host: %s:%s\r\n";
static const char *connection_header = "Connection: close\r\n";
static const char *proxy_connection_header = "Proxy-Connection: close\r\n";
static const char *blank_line = "\r\n";

/* 클라이언트의 요청을 처리하는 함수 */
void doit(int fd);
/* URI를 파싱하여 호스트 이름, 경로 및 포트 정보를 추출하는 함수 */
void parse_uri(char* uri, char* hostname, char* path, char* port);
/* 새로운 요청 헤더를 작성하는 함수 */
void build_request_header(char* request_header, char* hostname, char* path, char* port, rio_t* pclient_rio);
/* 클라이언트에게 오류 메시지를 전송하는 함수 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
/* 프로세스 동시성을 위한 시그널 핸들러 */
void sigchld_handler(int sig);

int main(int argc,char **argv)  // 인수로 포트 번호를 받음
{
  int listenfd, connfd;                     
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* 인수(프록시 서버가 수신 대기할 포트)가 올바른지 체크 */
  if (argc != 2) {
  fprintf(stderr, "usage: %s <port>\n", argv[0]);
  exit(1);
  }

  Signal(SIGCHLD, sigchld_handler); // 좀비 자식을 청소하는 SIGCHLD 핸들러
  listenfd = Open_listenfd(argv[1]);  // 지정된 포트에서 수신 대기하는 소켓을 열기
  /* 클라이언트 연결을 계속해서 수락할 무한루프 */
  while (1) {
  clientlen = sizeof(clientaddr);
  connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 연결 수락
  if (Fork() == 0) {
  Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트의 호스트 이름과 포트 번호를 가져옴
  printf("Accepted connection from (%s, %s)\n", hostname, port);
  Close(listenfd);  // (자식)리스닝 소켓을 종료
  doit(connfd); // (자식)클라이언트의 요청을 처리하고 응답을 반환하는 함수
  Close(connfd);  // (자식) 클라이언트와의 연결 종료
  exit(0);  // (자식)종료
  }
  Close(connfd);  // (부모)소켓을 닫아 클라이언트와의 연결을 종료
  }
}


void doit(int fd) 
{
  int serverfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port[MAXLINE], request_header[MAXLINE];
  rio_t rio, server_rio;

  /* 클라이언트의 요청 헤더를 읽고 분석 */
  Rio_readinitb(&rio, fd);  // fd를 rio_t 구조체의 rio_fd 필드에 저장하고, rio_cnt 필드를 0으로 설정하고, rio_bufptr 필드를 rio_buf로 설정
  if (!Rio_readlineb(&rio, buf, MAXLINE)) // fd에서 텍스트 라인을 읽어 버퍼에 저장. 실패(더 읽을 데이터가 없으면)하면 종료
    return;
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // 버퍼에서 읽어 들여, 메소드, uri, 버전에 집어 넣기
  if (strcasecmp(method, "GET")) {  // GET 메소드가 아닐 경우 에러 메시지 출력
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }                                                    

  /* GET 요청일 경우 URI를 파싱하여 호스트이름, 경로 및 포트 정보를 가져오기 */
  parse_uri(uri, hostname, path, port);

  /* 새로운 요청 헤더 작성 */
  build_request_header(request_header, hostname, path, port, &rio);

  /*원격 서버에 연결 */
  serverfd = Open_clientfd(hostname, port);
  if(serverfd<0){ // 실패 시 메시지 출력 후 종료
    printf("connection failed\n");
    return;
  }
  /* 서버에 헤더 작성하기 */
  Rio_readinitb(&server_rio, serverfd); // 구조체 초기화
  Rio_writen(serverfd, request_header, strlen(request_header)); // 소켓에 데이터 작성

  /* 서버로부터 메시지 받은 후 클라이언트에게 전송 */
  size_t n;
  while( (n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0 )  // server_rio에서 텍스트 라인 읽고 버퍼에 저장, 읽은 바이트 수를 n에 저장
  {
    printf("proxy received %zu bytes,then send\n",n); // n의 값을 10진수로 출력
    Rio_writen(fd, buf, n); // buf 내용을 fd에 쓴다.
  }
  Close(serverfd);
}


/*
 * parse_uri - parse the uri, get hostname, path and port information 
 */
void parse_uri(char* uri, char* hostname, char* path, char* port)
{
  char buf[MAXLINE];
  /*position to "//", the first"/", ":" */ 
  char* pstart, *ppath, *pport;

  strcpy(buf, uri);

  pstart = strstr(buf, "//") + 2; // 버퍼에서 '//' 문자열 검색하여 포인터 위치 + 2를 pstart에 저장
  ppath = strchr(pstart, '/');  // pstart에서 '/'을 찾아 포인터 위치를 ppath에 저장
  if(!ppath){ // 만약 '/'가 없으면 path에 '/' 복사
    strcpy(path, "/");
  }else{
    strcpy(path, ppath);  // path에 ppath를 복사하고 ppath가 가리키는 위치 값을 NULL로 설정
    *ppath = 0; // hostname에 pstart를 복사하기 전에 ppath가 가리키는 위치의 값을 NULL로 만들어 hostname에 복사되는 문자열이 '/' 이전까지만 복사되도록 하기 위함

  }

  pport = strchr(pstart, ':');  // pstart에서 ':'를 찾아 포인터 위치를 pport에 저장
  if(!pport){
    strcpy(port, "80"); // 만약 pport가 없으면 80을 포트에 복사
    strcpy(hostname, pstart); // hostname에 pstart를 복사
  }else{
    strcpy(port, pport+1);  // pport가 있으면 : 뒤의 데이터를 port에 복사
    *pport = 0; // pport가 가리키는 위치 값을 NULL로 설정
    strcpy(hostname, pstart); // hostname에 pstart를 복사
  }

}




/*
 * build_request_header - build request_header with the Host, 
 * User-Agent, Connection, and Proxy-Connection headers.
 * It is possible that web browsers will attach their own Host headers to their HTTP requests. 
 * If that isthe case, the proxy will use the same Host header as the browser.
 */
void build_request_header(char* request_header, char* hostname, char* path, char* port, rio_t* client_rio)
{
  char buf[MAXLINE], host_header[MAXLINE], other_header[MAXLINE];
  strcpy(host_header, "\0"); // 호스트 헤더에 NULL을 나타내는 이스케이프 시퀀스 복사
  /* path값을 형식 지정자를 사용하여 문자열로 변환하고, requestline_format 문자열에 삽입하여 형식화된 문자열을 만든 후 request_header 배열에 저장 */
  sprintf(request_header, requestline_format, path); 

  /* 호스트 헤더를 체크하고 client_rio를 위한 헤더를 얻어 바꾸기 */
  while(Rio_readlineb(client_rio, buf, MAXLINE) >0){  // 파일 디스크립터에 데이터가 있으면, 읽어서 버퍼에 데이터 저장
    if(strcmp(buf, blank_line)==0) break; //EOF(End of File)
    if(strncasecmp("Host:", buf, strlen("Host:"))){ // buf 문자열이 "Host:"로 시작하는지 확인.
      strcpy(host_header, buf); // 만약 아니라면 buf 내용을 host_header에 복사
      continue;
    }
    /* Connection:, Proxy-Connection:, User-Agent: 중 하나로 시작하는지 확인하고, 그렇다면 other_header에 buf 이어붙이기 */
    if( !strncasecmp("Connection:", buf, strlen("Connection:")) 
      && !strncasecmp("Proxy-Connection:", buf, strlen("Proxy-Connection:"))
      && !strncasecmp("User-Agent:", buf, strlen("User-Agent:")) )
    {
      strcat(other_header, buf);
    }
  }
  if(strlen(host_header) == 0){
    sprintf(host_header, host_header_format, hostname, port);
  }
  strcat(request_header, host_header);
  strcat(request_header, user_agent_header);
  strcat(request_header, other_header);
  strcat(request_header, connection_header);
  strcat(request_header, proxy_connection_header);
  strcat(request_header, blank_line);
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
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

void sigchld_handler(int sig)
{
  while (waitpid(-1, 0, WNOHANG) > 0); // 모든 자식 프로세스가 종료될 때까지 반복적으로 waitpid 함수 호출
  return;
}
