/* part 2. concurrent까지 구현 */

#include <stdio.h>
#include "csapp.h"

/* 캐시 : Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
int connect_endServer(char *hostname,int port,char *http_header);
void *thread(void *vargs);

int main(int argc, char **argv) {
  int listenfd, connfd;  // 클라이언트와 소켓 파일디스크립터
  char hostname[MAXLINE], port[MAXLINE]; // 클라이언트의 호스트명과 포트번호 저장
  socklen_t clientlen; // 클라이언트의 소켓 구조체 크기를 저장
  struct sockaddr_storage clientaddr; // 클라이언트의 소켓 주소를 저장
  pthread_t tid;  // 각 스레드를 식별할 변수

  /* command 명령 인자 갯수 체크 */
  if (argc != 2) { 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  /* 지정된 포트를 바인딩하고 대기 중인 소켓을 생성 */
  listenfd = Open_listenfd(argv[1]);
  /* 무한 루프 시작 */
  while (1) {
    clientlen = sizeof (clientaddr); // 주소 길이 계산
    /* 클라이언트의 연결 요청 대기 : 요청 발생 시 새로운 소켓(connfd) 생성 */
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    /* 소켓 주소 구조체 내용을 문자열로 변환 후, 연결 메세지 반환 */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    /* 🔥 여기서 쓰레드 생성 시작!!! 🔥 */
    Pthread_create(&tid, NULL, thread, (void *)connfd);  // 스레드 만들기
    // pthread 라이브러리를 사용하여 스레드를 생성하는 함수 
      // : 새로운 스레드를 생성하고, thread 함수를 실행하는데, 이 함수는 connfd를 인자로 받는다.
    // 첫 번째 인자(&tid) : 생성된 스레드의 식별자를 저장한 변수의 주소
    // 두 번째 인자(NULL) : 스레드의 속성(스택크기, 우선순위 등)을 저장하는 인자. 기본값이 NULL.
    // 세 번째 인자(thread) : 생성된 스레드가 실행할 함수의 포인터
    // 네 번째 인자(connfd) : thread 함수로 전달될 데이터. 
  }
  return 0;
}

/* 🔥 클라이언트 요청을 처리하기 위한 스레드 함수 */
void *thread(void *vargs) {
  int connfd = (int)vargs;
  Pthread_detach(pthread_self());
  // 분리형 함수(detachable) : 각 스레드가 독립적으로, 서로의 결과를 기다리지 않으므로 성능향상에 도움
  // 해당 스레드가 실행 종료 후 자동으로 메모리에서 제거 될 수 있도록 함.
  doit(connfd);
  close(connfd);
}

/* 🔥 클라이언트의 http 요청 처리 ; 근데 이걸 이제 웹서버로 연결 및 서버에게 객체 요청 */ 
void doit(int connfd) {
  int end_serverfd;  // 엔드서버 파일 디스크립터

  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char endserver_http_header[MAXLINE];

  char hostname[MAXLINE], path[MAXLINE];
  int port;

  rio_t rio, server_rio; /* rio: 클라이언트 rio, server_rio: 엔드서버(타이니) rio*/

  /* 1. 클라이언트가 보낸 요청 헤더에서 method, uri, version을 가져옴.*/
  Rio_readinitb(&rio, connfd);  // rio 구조체 초기화
  Rio_readlineb(&rio, buf, MAXLINE);  // http 요청 메세지 읽어옴. (buf 에서 client request 읽어들이기)
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET")) {
    printf("Proxy does not implement this method");
    return;
  }

  /* 2. 프록시 서버가 타이니 서버로 보낼 정보들을 파싱 */
  parse_uri(uri, hostname, path, &port);
  /* 3. 프록시 서버가 타이니 서버로 보낼 헤더들을 만듦 */
  build_http_header(endserver_http_header, hostname, path, port, &rio);
  /* 4. 프록시와 타이니를 연결 
    : connect_endServer 함수에서 서버와 연결에 성공하고 반환된 파일 디스크립터를 end_serverfd에 저장 */
  end_serverfd = connect_endServer(hostname, port, endserver_http_header); 
  if (end_serverfd < 0) {  // 연결 실패 시 에러메세지 반환
    printf("connection failed\n");
    return;
  }

  /* 5. 타이니에 http 요청 헤더를 보냄 */
  Rio_readinitb(&server_rio, end_serverfd);  // end_serverfd를 가리키는 server_rio 구조체 초기화
  Rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header));
  // end_serverfd 소켓 디스크립터에 endserver_http_header를 write함.(전송!) 
  
  /* 6. 타이니로부터 응답 메세지를 받아 클라이언트에 보내줌 */
  size_t n;  // rio함수가 읽은 바이트 수를 저장하는 함수
  while((n = Rio_readlineb(&server_rio,buf, MAXLINE)) != 0) {  // 서버의 응답을 한 줄씩 읽어옴.(0이 될 때까지)
    printf("proxy received %d bytes,then send\n",n);
    Rio_writen(connfd, buf, n);  // buf에는 타이니 서버에서 보낸 응답 데이터가 저장되어 있으며, 이것을 connfd 소켓에 n바이트 만큼 보냄
  }
  Close(end_serverfd);  // 타이니 서버와 연결된 소켓을 닫음
}

/* 🔥 타이니로 보낼 http 헤더 생성 함수 */
  // 응답 라인 만들고, 클라이언트 요청 헤더들에서 호스트 헤더와 나머지 헤더를 구분
void build_http_header(char *http_header,char *hostname,char *path,int port,rio_t *client_rio) {
  char buf[MAXLINE],request_hdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];

  /* 1. 응답 요청 라인 만들기 
    : 여기서 path는 클라이언트가 요청한 url 경로 */
  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);  // sprintf를 사용하여 request_hdr버퍼에 저장

  /* 2. 클라이언트 요청 헤더들에서 Host header와 나머지 header들을 구분하여 저장 */
  /* while 루프에서 클라이언트가 보낸 요청 헤더를 한 줄씩 읽어오면서 buf 변수에 저장 */
  while(Rio_readlineb(client_rio, buf, MAXLINE)>0) {  
    /* 탈출 조건 : EOF, '\r\n' 만나면 끝 */
    if (strcmp(buf, "\r\n") == 0) 
      break; 

    /* 호스트 헤더 찾기 
      : 반드시 있어야 하는 필수 헤더. 프록시는 이 정보를 사용해서 타이니와 연결을 맺고 요청을 전달! */
    if (!strncasecmp(buf, "Host", strlen("Host"))) { 
      // strcasecmp() : 문자열 비교 함수. 두 문자열이 일치하면 0을 반환. (buf 문자열이 "Host"로 시작하는지 확인)
      // 0이 반환되면 거짓이 되기 때문에 앞에 !를 붙여준다.
      strcpy(host_hdr, buf);  // host_hdr 변수에 buf의 내용을 복사
      continue;
    }
    /* 나머지 헤더 찾기 
      : 클라이언트의 요청 정보와 관련된 정보이나, 타이니 서버로 전달되어야 할 필요가 없거나, 프록시 서버에서 삭제하거나 변경해야 하는 헤더들 
        connection, proxy-connection, user-agent같은 헤더들은 타이니로 전달될 필요가 없으므로, 
        이 셋을 제외한 다른 헤더가 other_hdr 버퍼에 추가됨 */
    if (strncasecmp(buf, "Connection", strlen("Connection"))
        && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection"))
        && strncasecmp(buf, "User-Agent", strlen("User-Agent"))) {
      strcat(other_hdr,buf);  
    }
  }

  /* 클라이언트가 보낸 요청 헤더에서 host 헤더를 찾지 못했을 경우, host 헤더 생성! */
  if (strlen(host_hdr) == 0) {
      sprintf(host_hdr,"Host: %s\r\n",hostname);
  }

  /* 프록시 서버가 엔드 서버로 보낼 요청 헤더 작성 */
  sprintf(http_header,"%s%s%s%s%s%s%s",
          request_hdr,  // 클라이언트에서 전송된 http 요청 헤더
          host_hdr,  // 클라이언트에서 전송된 http 요청 중 host 헤더
          "Connection: close\r\n",  // 클라이언트와 서버 간의 연결을 끊겠다는 의미의 헤더
          "Proxy-Connection: close\r\n",  // 프록시와 클라이언트 간의 연결을 끊겠다는 의미의 헤더
          user_agent_hdr,  // 클라이언트에서 전송된 http 요청 중 user-agent 헤더
          other_hdr,// 클라이언트에서 전송된 http 요청 중 일부를 제외한 other 헤더
          "\r\n");
  return ;
}

/* 🔥 프록시와 타이니 서버 연결 함수
  : 주어진 호스트네임과 포트번호를 가지고 서버에 연결 */
inline int connect_endServer(char *hostname,int port,char *http_header) {
  char portStr[100];
  sprintf(portStr,"%d",port);  // portStr 문자열에 포트번호를 저장
  return Open_clientfd(hostname,portStr);  // open_clientfd를 호출하여 서버에 연결! 연결에 성공하면 파일 디스크립터가 반환됨.
}

/* 🔥 uri 파싱 함수 
  : 인자로 uri를 받아서 그 중에서 호스트이름(hostname)과 경로(path), 포트번호(port)를 추출해내는 함수 */
void parse_uri(char *uri,char *hostname,char *path,int *port) {
  *port = 80; // 포트번호를 80으로 초기화!

  char* pos = strstr(uri,"//");  // uri에서 '//' 문자열 검색(strstr)
  pos = pos!=NULL? pos+2:uri;  // 검색 결과가 null값이 아니면 '//'이후부터 사용하고, null 값이면 uri 시작부분부터 사용(pos에 저장)

  /* pos에서 다시 ':' 문자열 검색 */
  char *pos2 = strstr(pos,":"); 
  /* 만약 ':' 문자열이 있다면 */
  if (pos2!=NULL) { 
    *pos2 = '\0';  // pos2는 '\'문자열의 시작점을 가리킴
    sscanf(pos,"%s",hostname);  // 문자열 '//' 다음 내용을 hostname으로 추출
    sscanf(pos2+1,"%d%s",port,path);  // ':'다음 위치부터 정수형 데이터(%d)를 추출하여 port에 저장하고, 남은 문자열은 %s형식에 맞게 추출하여 path에 저장
  } // 예시) http://example.com:8080/test
  /* 만약 ':'문자열이 없다면 */ 
  else {
    pos2 = strstr(pos,"/");  // pos 변수에 할당된 문자열에서 '/'를 찾음
    if (pos2!=NULL) {  // '/'이 있으면 이전까지의 문자열을 호스트 이름으로, 이후의 문자열은 경로로 추출
      *pos2 = '\0';
      sscanf(pos,"%s",hostname);
      *pos2 = '/';
      sscanf(pos2,"%s",path);
    } else {  // 문자열 '/'도 없고 ':'도 없다면 호스트 이름만 추출!
      sscanf(pos,"%s",hostname);
    }
  }
  return;
}

//---------------------------------

// 캐싱 : 
// 1. 캐싱 가능한 웹 객체 크기(max object size), 캐시된 객체들의 크기 합은 max cache size를 넘을 수 없도록 구현
// 2. LRU(least recently used) 삭제 정책을 적용
// 3. 캐시 접근이 스레드 세이프 하도록 구현(여러 스레드에서 동시에 접근해도 프로그램이 안전해야 함.)