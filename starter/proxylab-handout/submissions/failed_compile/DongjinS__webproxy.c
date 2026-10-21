#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000 // cache 사용 안함
#define MAX_OBJECT_SIZE 102400 // cache 사용 안함

/* You won't lose style points for including this long line in your code */
// 코드 상에서 사용 안함 - 안해도 작동에는 문제 없지만 실제로 없어도 되는지 궁금함..
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int connfd); // 클라이언트와 연결 후 작동
void *thread(void *vargp); // 클라이언트와 연결 후 스레드 분리
int separate_uri(char *uri, char *host, char *port, char *path); // 클라이언트에게 요청받은 uri를 host port path로 분할

int main(int argc, char **argv) {
  int listenfd, *connfd; // 듣기 소켓 연결 소켓 식별자
  pthread_t tid; // thread id 
  
  socklen_t clientlen; // 소켓 구조체 길이
  struct sockaddr_storage clientaddr; // 클라이언트 정보 저장할 소켓 구조체

  if (argc != 2) { // proxy 를 실행할 때 인자 두개 - ./proxy, 포트 번호를 안줬다면 defalut 5000포트 연다.
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    fprintf(stderr, "use default port 5000\n");
    listenfd = Open_listenfd("5000"); // 듣기 소켓 연다
  } else {
    listenfd = Open_listenfd(argv[1]); // 포트번호를 줬으면 그대로 듣기 소켓을 연다.
  }


  while (1) { // 클라이언트로 부터 연결 기다린다.
    // wait for connection as a server
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Malloc(sizeof(int)); // malloc 으로 반복 시 마다 할당해줘야 스레드마다 다른 주소의 connfd 가 들어가서 위험성을 피함
    *connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen); // 연결되면 연결 소켓 열고 식별자 지정
    Pthread_create(&tid, NULL, thread, connfd); // 스레드 생성 + thread 함수 실행
  }

}

void *thread(void *vargp){
  int connfd = *((int *)vargp); // 연결소켓 식별자 넘겨 받고
  Pthread_detach(pthread_self()); // 스레드 분리
  Free(vargp); // 인자로 넘어온 포인터 할당 해제
  doit(connfd); // 분리 후 doit 실행
  Close(connfd); // doit 후 연결 소켓 닫기
  return;
}

void doit(int connfd){
  int clientfd; // 최종 서버로 보낼 클라이언트 소켓 식별자
  struct sockaddr_storage clientaddr; // 클라이언트 정보(나 자신) 저장할 구조체
  char c_buf[MAXLINE], s_buf[MAXLINE]; // client buffer, server buffer
  ssize_t sn, cn; //프록시가 sever로서 읽고 쓸 개수, client로서 읽고 쓸 개수
  rio_t client_rio, server_rio; // 프록시가 클라이언트로서, 서버로서 각각 rio로 주고 받을 rio 구조체

  char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; //메소드 uri version 저장할 배열
  char host[MAXLINE], port[MAXLINE], path[MAXLINE]; // 호스트 port path 저장할 배열

  Rio_readinitb(&server_rio, connfd); // server_rio와 연결 소켓 rio연결(클라이언트-프록시)
    /*
     * if uri is full path url like http://localhost:8000/server.c
     * remove host part http://localhost:8000
     * only pass /server.c to server
     */
    // parse HTTP request first line
    if (!Rio_readlineb(&server_rio, s_buf, MAXLINE)) {//클라이언트로 부터 리퀘스트 첫줄 읽기
      return; // 실패하면 리턴
    }
    sscanf(s_buf, "%s %s %s", method, uri, version); // 리퀘스트 첫줄 메소드, uri, version으로 분리
    
    memset(host, '\0', MAXLINE); // 호스트 포트 경로 배열의 모든 요소 \0으로 초기화
    memset(port, '\0', MAXLINE);
    memset(path, '\0', MAXLINE);
    int res; // uri 분리 결과 저장
    if ((res = separate_uri(uri, host, port, path)) == -1) { //uri분리가 올바르지 않다면(-1,0) 에러 출력 후 doit 종료
      fprintf(stderr, "not http protocol\n");
      return;
    } else if (res == 0) {
      fprintf(stderr, "not a abslute request path\n");
      return;
    }

    // connect server as a client
    if ((clientfd = Open_clientfd(host, port))<0){// 클라이언트 소켓 열고(connndct 시도 까지) 식별자 지정(프록시 - 서버)
      printf("connection failed\n");  // 연결 실패시 에러 출력하고 doit 종료
      return;
    } 
    Rio_readinitb(&client_rio, clientfd); // 클라이언트 소켓과 client_rio 연결

    /*
     *  browser  -->  proxy  -->  server
     *
     *  send requests
     */

    // write first request line
    sprintf(s_buf, "%s %s %s\n", method, path, version); //s_buf에 method path version 저장 (리퀘스트 헤더 첫 줄)
    Rio_writen(clientfd, s_buf, strlen(s_buf)); // s_buf에 저장된 내용 클라이언트 소켓에 쓴다.(최종 서버에 보냄)
    printf("%s", s_buf); // 리퀘스트 헤더 출력 첫줄
    do {
      // pass next http requests
      sn = Rio_readlineb(&server_rio, s_buf, MAXLINE); // 클라이언트의 나머지 리퀘스트 모두 읽어서 s_buf에 저장
      printf("%s", s_buf); // 리퀘스트 출력
      Rio_writen(clientfd, s_buf, sn); // s_buf에 저장된 내용 클라이언트 소켓에 쓴다.(최종 서버에 보냄)
    } while(strcmp(s_buf, "\r\n")); // \r\n 나올때 까지 위의 내용 반복

    /*
     *  server  -->  proxy  -->  browser
     *
     *  server send response back
     */
    while ((cn = Rio_readlineb(&client_rio, c_buf, MAXLINE)) != 0) // 최종 서버의 리스폰스 읽고 c_buf에 저장 - 0개가 올때까지
      Rio_writen(connfd, c_buf, cn); // 클라이언트와 연결된 연결소켓을 통해 c_buf에 저장해 놓은 최종 서버의 리스폰스를

    Close(clientfd); // 최종 서버로부터 리스폰스 끝났으면 소켓 닫기
}

/*
 * if uri is abslute path url like
 *   http://localhost:8888/something
 *   or
 *   http://localhost/something (port default is 80)
 * separate into three part and return 1
 *
 * if uri is relative path like /something
 * do nothing and return 0
 *
 * if uri is abslute path and not http protocal like https/ftp/etc
 * do nothing, return -1, it's error
 */
int separate_uri(char *uri, char *host, char *port, char *path) { // uri를 host port path 로 분리
  // relative path
  if (uri[0] == '/') // uri의 시작이 /이라면 0리턴
    return 0;

  // abslute path
  char *prefix = "http://";
  int prelen = strlen(prefix);
  // if not http protocal, error
  if (strncmp(uri, prefix, prelen) != 0) //uri의 시작이 http://가 아니라면 return -1
    return -1;

  char *start, *end;
  start = uri + prelen; // start - http:// 다음
  end = start;

  // copy host
  while (*end != ':' && *end != '/') { // port 전 혹은 path 전까지 end++
    end++;
  }
  strncpy(host, start, end-start); // start부xj end에서 start 뺀만큼 host에 복사 http://(start)|host|(end):80 

  // port is provided
  if (*end == ':') { // :이 있으면 = port 가 있으면
    // skip ':'
    ++end;
    start = end;    // start = end +1 = 포트 시작
    // copy port
    while (*end != '/') // /가 나타날때까지 end++
      end++;
    strncpy(port, start, end-start); // 포트 복사
  } else {
    // port is not provided, defualt 80 - 포트 없으면 80으로 넣는다.
    strncpy(port, "80", 2);
  }

  // copy path
  strcpy(path, end); //포트 뒤에 남은것들 path로 복사
}

