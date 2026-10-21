/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);
// void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void get_filetype(char *filename, char *filetype);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

/* 소켓 정보 가져오게 하고 할 일 하게하는 main 함수 */
int main(int argc, char **argv)
{
  int listenfd, connfd;                  // 듣기 식별자, 연결 식별자 (서버 소켓과 클라이언트 소켓)
  char hostname[MAXLINE], port[MAXLINE]; // 클라 호스트이름, 포트 (혹은 아이피 주소, 서비스 이름)
  socklen_t clientlen;                   // 클라 주소 크기 (sockaddr 구조체 크기)
  struct sockaddr_storage clientaddr;    // 클라 주소 정보 구조체 (IPv4/IPv6 호환)

  /* 인자 맞게 썼는지 확인 */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 연결 대기 시작
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                       // line:netp:tiny:accept // 연결 수립
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 소켓에서 클라 호스트 이름과 포트 번호 가져오기
    printf("Accepted connection from (%s %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit // 연결 수립 후 서버에서 할 일
    Close(connfd); // line:netp:tiny:close // 할 일 다 하면 연결 끝
  }
}

/* 오류 있는지, 컨텐츠가 정적인지 동적인지 확인하고 읽기/실행 */
void doit(int clientfd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;
  char port[5] = "8080"; // 포트는 8080으로 고정하라고 적혀있었음

  /* 클라에게서 받은 요청 라인과 헤더 읽기 */
  rio_readinitb(&rio, clientfd);           // 파일 식별자에 있는 데이터를 rio 구조체와 연결
//   rio_readlineb(&rio, buf, MAXLINE); // 한 줄 읽어와 버퍼에 저장
//   printf("클라에게서 받은 요청 라인:\n");
//   printf("%s\n", buf);                           // 요청 라인 출력
//   sscanf(buf, "%s %s %s", method, uri, version); // 버퍼에서 데이터 읽고 method, uri, version에 저장
//   if (strcasecmp(method, "GET")) // GET 요청인지 확인
//   {
//     clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method"); // 아니면 꺼지라고 함
//     return;
//   }
  
  int serverfd = open_clientfd("localhost", port); // 서버와 소켓 연결
  request_to_server(&rio, serverfd); // 클라에게서 받은 요청을 서버로 넘긴다
//   parse_uri(uri, filename, cgiargs); // URI에서 데이터 추출
  response_to_client(clientfd, serverfd); // 서버에게서 받은 응답을 클라로 넘긴다
}

/* 요청 헤더를 읽는 함수 */
void request_to_server(rio_t *rp, int serverfd)
{
  // 요청 헤더들을 읽어서 바로 서버 소켓으로 보내보자.
  // rp는 클라한테 받은 데이터들 들어있는 버퍼임.

  char buf[MAXLINE];

  rio_t rio; // 구조체. 버퍼링된 입출력 처리.
  rio_readinitb(&rio, serverfd); // 소켓 식별자를 이용한 읽기 작업 준비
//   rio_readlineb(rp, buf, MAXLINE); // 요청 라인을 읽어서 buf에 넣음
  while(strcmp(buf, "\r\n")) // 버퍼에 빈줄이 나올 때까지 반복
  {
    rio_readlineb(rp, buf, MAXLINE); // 요청 헤더들을 읽어서 buf에 넣음
    rio_writen(serverfd, buf, strlen(buf)); // buf에 넣은 걸 바로 서버에 냅다 던져
    printf("test_line: %s", buf);
  }
  return;
}

/* uri에 담긴 정보 추출 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin")) // 정적 컨텐츠를 요청했다면
  {
    strcpy(cgiargs, ""); // 초기화
    strcpy(filename, "."); // .으로 초기화
    strcat(filename, uri); // uri에서 요청하는 디렉토리로 감
    if (uri[strlen(uri)-1] == '/') // uri 마지막 문자가 / 이면
      strcat(filename, "home.html"); // home.html 꺼낸다
    return 1;
  }
  else // 동적 컨텐츠를 요청했다면
  {
    ptr = index(uri, '?'); // ? 뒤에는 프로그램 매개변수들이 나옴
    if (ptr) // 매개변수가 있으면
    {
      strcpy(cgiargs, ptr + 1); // cgi 매개변수 담는 곳에 넣어준다
      *ptr = '\0'; // 넣어주고 난 후엔 매개변수 정보들 삭제함
    }
    else // 매개변수가 없으면
      strcpy(cgiargs, ""); // 매개변수 없다고 초기화시키기
    strcpy(filename, "."); // filename에 . 넣고
    strcat(filename, uri); // .uri 만들기
    return 0;
  }
}

/* 서버의 응답을 클라이언트로 전송 */
void response_to_client(int clientfd, int serverfd)
{
    char buf[MAXBUF];
    rio_t rio;

    rio_readinitb(&rio, serverfd); // 서버 소켓에서 들어온 응답을 읽을 준비를 한다

    // 서버로부터 데이터를 줄 단위로 계속 읽어옴
    ssize_t n; // 읽은 바이트 수를 저장할 변수
    while ((n = rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        rio_writen(clientfd, buf, n); // 클라이언트로 데이터 전송
        printf("%s", buf); // 디버깅을 위해 출력

        // 헤더 끝을 인식하고, 이후 본문도 처리하기 위해 루프를 계속 유지
    }

    printf("클라에게 전송 완료\n");
}

/* 해당 파일의 MIME 타입 반환 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filename, ".image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

/* 에러 메시지 띄우는 함수 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* HTTP 응답 중 body 만들기 */
  // body를 다시 추가해서 안 넣어주면 마지막 빼고 다 사라져버림.
  // sprintf는 기존에 있던 데이터에 추가되는 방식이 아니라 덮어씌워버리기 때문.
  // 응답을 문자열 하나로 만들어서 크기를 쉽게 알 수 있게 함.
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* HTTP 응답 출력 */
  // 응답 시작 라인, 응답 헤더 뿌려준 후에 응답 body 보내줌
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}