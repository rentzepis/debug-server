#include <stdio.h>
#include "csapp.h"
#include <signal.h>


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const int is_local_test = 1; 

void doit(int clientfd);
void read_requesthdrs(rio_t *rp, void *buf, int serverfd, char *hostname, char *port);
int parse_uri(char *uri, char *hostname, int *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);

int main(int argc, char **argv) {

  int listenfd, clientfd;
  char client_hostname[MAXLINE], client_port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  // pthread_t tid;
  Signal(SIGPIPE, SIG_IGN);
  // Signal(SIGINT, sigint_handler);


  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); //브라우저로부터의 연결을 받을 준비를 함
  while(1){ // 열어놓은 포트로부터의 연결을 계속 받음
    clientlen = sizeof(clientaddr);
    // clientfd = Malloc(sizeof(int)); // clientfd에 int형의 크기만큼 메모리 할당
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //브라우저로부터의 연결을 받음
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
    
    
    // Pthread_create(&tid, NULL, thread, clientfd); 
    doit(clientfd); 
    Close(clientfd);
  }
  return 0;
  // printf("%s", user_agent_hdr);
}

void doit(int clientfd){
  int serverfd, content_length; 
  // struct stat sbuf; //파일의 상태를 저장하는 구조체
  char request_buf[MAXLINE], response_buf[MAXLINE]; //요청을 저장하는 버퍼와 응답을 저장하는 버퍼
  char method[MAXLINE], uri[MAXLINE], hostname[MAXLINE], path[MAXLINE], version[MAXLINE]; 
  char *response_ptr;
  int port;
  rio_t request_rio, response_rio;

  memset(request_buf, 0, MAXLINE);
  memset(response_buf, 0, MAXLINE);
  /* 헤더로부터 요청 라인 읽기 / 클라이언트의 요청을 프록시에서 읽고 분석 */  
  Rio_readinitb(&request_rio, clientfd); //rio의 주소에 한 개의 빈 버퍼 설정, 해당 버퍼와 한 개의 오픈한 파일 식별자 연결
  Rio_readlineb(&request_rio, request_buf, MAXLINE); //rio의 버퍼에서 한 개의 텍스트 라인을 읽어 buf에 저장
  printf("Request headers:\n %s \n", request_buf); //buf(요청) 출력
 
  //요청 라인을 parsing하여 method, uri, hostname, port, path를 추출
  sscanf(request_buf, "%s %s %s", method, uri, version); //request_buf에서 문자열을 읽어 method, uri에 저장
  parse_uri(uri, hostname, &port, path);

  // Server에 전송할 내용 저장 -> 요청라인 형식을 GET /urihere/ HTTP/1.0 (method uri version)으로 변경 
  sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0"); //request_buf에 문자열 저장

  if (strcasecmp(method, "GET")) { //method가 GET이 아니면
    clienterror(clientfd, method, "501", "Not Implemented", //에러메시지 출력
                "Tiny does not implement this method"); 
    return;
  }

  // 서버 소켓 생성 (Proxy->Server)
  serverfd = is_local_test ? Open_clientfd(hostname, port) : Open_clientfd("127.0.0.1", port); //서버에 연결
  if (serverfd < 0) {
    clienterror(serverfd, method, "502", "Bad Gateway", "Failed to establish connection with the end server");
    return;
  }

  // Rio_readinitb(&request_rio, serverfd); //rio의 주소에 한 개의 빈 버퍼 설정, 해당 버퍼와 한 개의 오픈한 파일 식별자 연결
  Rio_writen(serverfd, request_buf, strlen(request_buf)); //rio의 버퍼에 request_buf의 내용을 쓰기

  // Request Header 읽기 및 전송 (클라에서 받은 걸 서버로 전송)
  read_requesthdrs(&request_rio, request_buf, serverfd, hostname, port); //rio의 헤더를 읽어서 버퍼에 저장

  // Response Header 읽기 및 전송 (서버에서 프록시로 받은 것을 다시 클라이언트로 전송)
  Rio_readinitb(&response_rio, serverfd); //rio의 주소에 한 개의 빈 버퍼 설정, 해당 버퍼와 한 개의 오픈한 파일 식별자 연결
  while(strcmp(response_buf, "\r\n")){ //response_buf가 "\r\n"이 아니면
    Rio_readlineb(&response_rio, response_buf, MAXLINE); //rio의 버퍼에서 한 개의 텍스트 라인을 읽어 buf에 저장
    if (strstr(response_buf, "Content-Length")) {
      content_length = atoi(strchr(response_buf, ':')+ 1);
    }
    Rio_writen(clientfd, response_buf, strlen(response_buf)); //rio의 버퍼에 response_buf의 내용을 쓰기
  }

  // Response Body 읽기 및 전송 (서버에서 프록시로 받은 것을 다시 클라이언트로 전송)
  response_ptr = (char*)malloc(content_length);
  Rio_readnb(&response_rio, response_ptr, content_length);
  Rio_writen(clientfd, response_ptr, content_length);

  free(response_ptr);
  Close(serverfd);
}

void clienterror(int fd, char*cause, char *errnum, char *shortmsg, char *longmsg){

  char buf[MAXLINE], body[MAXBUF];

  /* HTTP 응답 바디 생성 - Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>"); //body에 문자열 저장
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body); //body에 문자열 저장
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg); //body에 문자열 저장
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause); //body에 문자열 저장
  sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body); //body에 문자열 저장

  /* HTTP 응답 헤더 생성 - Build the HTTP response header */
  sprintf(buf, "%s %s %s\r\n", "HTTP/1,0", errnum, shortmsg); //buf에 문자열 저장
  Rio_writen(fd, buf, strlen(buf)); //rio의 버퍼에 buf의 내용을 쓰기
  sprintf(buf, "Content-type: text/html\r\n"); //buf에 문자열 저장
  Rio_writen(fd, buf, strlen(buf)); //rio의 버퍼에 buf의 내용을 쓰기
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body)); //buf에 문자열 저장
  Rio_writen(fd, buf, strlen(buf)); //rio의 버퍼에 buf의 내용을 쓰기
  Rio_writen(fd, body, strlen(body)); //rio의 버퍼에 body의 내용을 쓰기
}


//uri의 형태는 http://hostname:port/path 형태로 되어있음
int parse_uri(char *uri, char *hostname, int *port, char *path){
  
  char *hostname_ptr = strstr(uri, "//")? strstr(uri, "//") + 2 : uri; // uri에서 "//"의 위치를 가리키는 포인터
  char *port_ptr = strstr(hostname_ptr, ":"); //port를 가리킬 포인터
  char *path_ptr = strstr(hostname_ptr, "/"); //path를 가리킬 포인터
  strcpy(path, path_ptr); //path에 path_ptr의 내용을 복사

  if(port_ptr){ //port를 받은 경우
    strncpy(port, port_ptr+1, path_ptr - port_ptr-1 ); //port에 port_ptr+1의 내용을 path_ptr-port_ptr-1만큼 복사
    strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr); //hostname에 hostname_ptr의 내용을 port_ptr-hostname_ptr만큼 복사
  }
  else{ // 포트를 받지 않은 경우 
    if (is_local_test)
      strcpy(port, "80"); //port의 기본값
    else
      strcpy(port, "4500");
    strncpy(hostname, hostname_ptr, path_ptr-hostname_ptr); //hostname에 hostname_ptr의 내용을 path_ptr-hostname_ptr만큼 복사
  }
}

void read_requesthdrs(rio_t *request_rio, void *request_buf, int serverfd, char *hostname, char *port)
{
  int is_host_exist;
  int is_connection_exist;
  int is_proxy_connection_exist;
  int is_user_agent_exist;

  Rio_readlineb(request_rio, request_buf, MAXLINE); // 첫번째 줄 읽기
  while (strcmp(request_buf, "\r\n"))
  {
    if (strstr(request_buf, "Proxy-Connection") != NULL)
    {
      sprintf(request_buf, "Proxy-Connection: close\r\n");
      is_proxy_connection_exist = 1;
    }
    else if (strstr(request_buf, "Connection") != NULL)
    {
      sprintf(request_buf, "Connection: close\r\n");
      is_connection_exist = 1;
    }
    else if (strstr(request_buf, "User-Agent") != NULL)
    {
      sprintf(request_buf, user_agent_hdr);
      is_user_agent_exist = 1;
    }
    else if (strstr(request_buf, "Host") != NULL)
    {
      is_host_exist = 1;
    }

    Rio_writen(serverfd, request_buf, strlen(request_buf)); // Server에 전송
    Rio_readlineb(request_rio, request_buf, MAXLINE);       // 다음 줄 읽기
  }

  // 필수 헤더 미포함 시 추가로 전송
  if (!is_proxy_connection_exist)
  {
    sprintf(request_buf, "Proxy-Connection: close\r\n");
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }
  if (!is_connection_exist)
  {
    sprintf(request_buf, "Connection: close\r\n");
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }
  if (!is_host_exist)
  {
    if (!is_local_test)
      hostname = "127.0.0.1";
    sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }
  if (!is_user_agent_exist)
  {
    sprintf(request_buf, user_agent_hdr);
    Rio_writen(serverfd, request_buf, strlen(request_buf));
  }

  sprintf(request_buf, "\r\n"); // 종료문
  Rio_writen(serverfd, request_buf, strlen(request_buf));
  return;
}


//    if(!strstr(uri, "cgi-bin")){ //uri에 "cgi-bin"이 없으면
//     strcpy(cgiargs, ""); //cgiargs에 "" 저장
//     strcpy(filename, "."); //filename에 "." 저장
//     strcat(filename, uri); //filename에 uri를 이어붙임
//     if(uri[strlen(uri)-1] == '/') //uri의 마지막 문자가 '/'이면
//       strcat(filename, "home.html"); //filename에 "home.html"을 이어붙임
//     return 1; //1 반환
//   }
//   else{
//     ptr = index(uri, '?'); //uri에서 '?'의 위치를 ptr에 저장
//     if(ptr){ //ptr이 NULL이 아니면
//       strcpy(cgiargs, ptr+1); //cgiargs에 ptr+1의 내용을 복사
//       *ptr = '\0'; //ptr에 '\0' 저장
//     }
//     else
//       strcpy(cgiargs, ""); //cgiargs에 "" 저장
//     strcpy(filename, "."); //filename에 "." 저장
//     strcat(filename, uri); //filename에 uri를 이어붙임
//     return 0; //0 반환
//   }
// }