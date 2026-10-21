#include "csapp.h"
#include "cache.h"
#include <stdio.h>

#define FILE_NAME_SIZE 4096

/* 프록시가 웹 서버에 보낼 자신에 대한 정보 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *new_version = "HTTP/1.0";

void do_it(int fd);
void do_request(int clientfd, char *method, char *uri_ptos, char *host);
void do_response(int connfd, int clientfd, char *host, char *uri);
int parse_uri(char *uri, char *uri_ptos, char *host, char *port);
void *thread(void *vargp); // vargp: void argument pointer3

/* 연결 및 병행성 관리 */
int main(int argc, char **argv)
{
  // #region 변수 선언부
  int listenfd = 0, *connfdp = NULL;
  char hostname[MAXLINE], port[MAXLINE];

  /* 소켓주소 구조체의 길이를 나타내는 전용데이터 타입. 부호없는 32비트 이상의 정수 */
  socklen_t clientlen;
  /* 범용 소켓 주소 구조체 */
  struct sockaddr_storage cliendtaddr = {};
  /* 스레드를 식별하기 위한 데이터 타입 */
  pthread_t tid;
  printf("%d 🐛 [main] argc: %d\n", __LINE__, argc);
  // #endregion

  // #region 포트번호 넣었는지 확인
  /* 프로그램이 올바르게 실행되었는지 확인. 예) ./proxy 8080 ..사용자가 포트번호 빼먹고 실행할 수도 있어서 확인한다 */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    /* 프로그램 완전 종료. exit(0): 정상 종료, exit(1): 에러로 인한 종료 */
    exit(1);
  }
  // #endregion

  cache_init();

  /*================== 👷 1. 리스닝 소켓 만들기 ==================*/
  /* 포트번호에 해당하는 리스닝 소켓 식별자를 열어준다 */
  listenfd = Open_listenfd(argv[1]);
  printf("\n✅ ================ Proxy Started. ================ \n");
  printf("%d 🐛 [main] listenfd: %d\n", __LINE__, listenfd);

  /* 클라이언트의 요청이 올 때마다 새로운 커넥션 소켓을 만들고 doit() 호출 */
  while (1)
  {
    /*================== 👷 2. 커넥션 소켓 만들기 ==================*/
    /* 멀티스레딩 환경에서는 각 스레드가 독립적인 소켓을 가져야 해서 malloc이 필수 */
    connfdp = Malloc(sizeof(int));
    /* 클라이언트에게서 받은 연결 요청을 accept하고 개별 커넥션 소켓 생성 */
    clientlen = sizeof(cliendtaddr);
    *connfdp = Accept(listenfd, (SA *)&cliendtaddr, &clientlen);

    printf("%d 🐛 [main] Accept() connfd: %d\n", __LINE__, *connfdp);

    // #region 연결 성공 메세지 출력
    Getnameinfo((SA *)&cliendtaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("%d 🐛 [main] Accepted connection from (%s, %s)\n", __LINE__, hostname, port);
    // #endregion

    /*================== 👷 3.스레드 생성 ==================*/
    Pthread_create(&tid, NULL, thread, connfdp); // 생성된 스레드를 저장할 포인터, 스레드 속성 설정, 스레드 생성 후 실행할 함수, 함수에 전달할 인자
  }
}

/* per client thread logic */
void *thread(void *vargp)
{
  /* 클라이언트-프록시 간 통신소켓 */
  int connfd = *((int *)vargp);
  printf("%d 🐛 [thread] connfd: %d\n", __LINE__, connfd);

  /* 스레드를 분리 상태로 설정해서 스레드 종료후 자동으로 리소스가 회수되도록 한다 */
  Pthread_detach(pthread_self());
  /* Malloc()으로 할당한 메모리 해제 */
  Free(vargp); // Phread_create 스레드는 자신의 리소스만 관리하고, 동적 할된 메모리는 자동해제하지 않으므로 명시적으로 해제해야 함

  do_it(connfd);
  Close(connfd);
  return NULL;
}

/* Core client-proxy-server transaction */
void do_it(int connfd)
{
  // #region 변수 선언부
  int clientfd = 0;
  char buf[MAXLINE], host[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], uri_server_to_proxy[MAXLINE];
  rio_t rio;
  // #endregion

  printf("%d 🐛 Open_clientfd() connfd: %d\n", __LINE__, connfd);

  /*================== 👷 1.host,port,path 추출 ==================*/

  /* rio 버퍼와 fd(proxy의 connfd)를 연결시켜준다 */
  Rio_readinitb(&rio, connfd);
  /* rio에있는 내용을 모두 buf로 옮긴다 */
  Rio_readlineb(&rio, buf, MAXLINE);

  printf("%d 🐛 [do_it] buf: %s\n", __LINE__, buf);

  /* buf에서 문자열을 읽어와서 각 변수에 저장 */
  sscanf(buf, "%s %s %s", method, uri, version);
  printf("%d 🐛 [do_it] method: %s, uri: %s, version: %s\n", __LINE__, method, uri, version);

  int result = parse_uri(uri, uri_server_to_proxy, host, port);
  printf("%d 🐛 [do_it] host: %s, uri_ptos: %s, port: %s\n", __LINE__, host, uri_server_to_proxy, port);

  /* server의 리스닝 소켓 연결 */
  clientfd = Open_clientfd(host, port);
  printf("%d 🐛 [do_it] Open_clientfd() clientfd: %d\n", __LINE__, clientfd);

  /*===========👷 2. 클라이언트의 요청 읽고 서버에 전달 =========*/
  do_request(clientfd, method, uri_server_to_proxy, host);

  /*===========👷 3. 서버의 응답을 클라이언트에 전달 (캐시 여부 확인) =========*/
  int is_cached = isInCache(host, uri_server_to_proxy);
  if (is_cached)
  {
    send_cached_response(clientfd, uri_server_to_proxy);
  }
  else
  {
    do_response(connfd, clientfd, host, uri_server_to_proxy);
  }

  /* 리스닝 소켓 연결 종료 */
  Close(clientfd);
}

void send_cached_response(int connfd, char *filepath)
{
  char buf[MAX_CACHE_SIZE];
  ssize_t n;

  /*============= 👷 1. 캐시 파일 열기 =============*/
  FILE *pFile = fopen(filepath, "rb");
  if (pFile == NULL)
  {
    printf("%d ❌ [send_cached_response] 파일 열기 실패: %s\n", __LINE__, filepath);
    return;
  }

  /*============= 👷 2. 파일에서 데이터 읽기 =============*/
  n = fread(buf, 1, MAX_CACHE_SIZE, pFile);
  if (n < 0)
  {
    printf("%d ❌ [send_cached_response] 파일 읽기 실패\n", __LINE__);
    fclose(pFile);
    return;
  }

  /*============= 👷 3. 응답을 클라이언트로 보내줌 =============*/
  Rio_writen(connfd, buf, n);
  printf("%d 🐛 [send_cached_response] 클라이언트에 응답 전송 완료\n", __LINE__);

  fclose(pFile);
}

/* proxy => server */
void do_request(int clientfd, char *method, char *uri_ptos, char *host)
{
  char buf[MAXLINE];
  printf("%d 🐛 [do_request] clientfd: %d\n", __LINE__, clientfd);
  printf("%d 🐛 [do_request] method: %s, uri_ptos: %s, host: %s\n", __LINE__, method, uri_ptos, host);

  /*================== 👷 1. 요청 헤더 읽고 요청 구조화 ==================*/
  int offset = 0;
  // GET /index.html HTTP/1.0
  offset += snprintf(buf + offset, MAXLINE - offset, "GET %s %s\r\n", uri_ptos, new_version);
  // Host: www.google.com
  offset += snprintf(buf + offset, MAXLINE - offset, "Host: %s\r\n", host);
  // User-Agent: ~(bla bla)
  offset += snprintf(buf + offset, MAXLINE - offset, "%s", user_agent_hdr);
  // Connections: close
  offset += snprintf(buf + offset, MAXLINE - offset, "Connection: close\r\n");
  // Proxy-Connection: close
  offset += snprintf(buf + offset, MAXLINE - offset, "Proxy-Connection: close\r\n\r\n");

  printf("%d 🐛 [do_request] 🔽 buf 🔽 \n%s\n", __LINE__, buf);

  /*============= 👷 2. 구조화한 요청을 서버로 전송 =============*/
  Rio_writen(clientfd, buf, (size_t)strlen(buf));
}

/* server => proxy */
void do_response(int connfd, int clientfd, char *host, char *uri)
{
  char buf[MAXBUF];
  ssize_t n;
  rio_t rio;

  printf("%d 🐛 [do_response] connfd: %d, clientfd: %d\n", __LINE__, connfd, clientfd);

  /*============= 👷 1. 서버로부터 온 응답 읽기 =============*/
  Rio_readinitb(&rio, clientfd);
  n = Rio_readnb(&rio, buf, MAXBUF);

  printf("%d 🐛 [do_response] read bytes: %zd\n", __LINE__, n);
  // 🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨
  // TODO: header+body를 다 가져와서 전체 메시지 크기를 계산하는 함수 구현
  // TODO: write_file에 넣어주기
  // 🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨🚨

  // printf("%d 🐛 [do_response] 🔽 buf 🔽 \n%s\n", __LINE__, buf);

  /*============= 👷 2. 서버로부터 온 응답 읽기 =============*/
  int can_cache = canAddToCache(host, uri, n);
  switch (can_cache)
  {
  case CACHE_OK:
    add_cache_entry(host, uri, n);
    break;

    // default:
    //   break;
  }

  /*============= 👷 2. 응답을 클라이언트로 보내줌 =============*/
  Rio_writen(connfd, buf, n);
}

void write_file(char *filepath, char *buf, int buf_size)
{
  /* 파일 쓰기 모드로 열기 */
  FILE *pFile = fopen(filepath, "wb");
  if (pFile == NULL)
  {
    printf("%d ❌ [write_file] 파일 열기 실패: %s\n", __LINE__, filepath);
    return;
  }

  /* buf 데이터를 파일에 쓰기 */
  size_t written = fwrite(buf, 1, buf_size, pFile);

  if (written != buf_size)
  {
    printf("쓰기 실패: %zu/%d 바이트만 썼습니다\n", written, buf_size);
  }
  else
  {
    printf("파일에 %d 바이트를 성공적으로 썼습니다\n", buf_size);
  }

  fclose(pFile); // 파일 닫기
}

int parse_uri(char *uri, char *uri_proxy_to_server, char *host, char *port)
{
  char *ptr = NULL;

  printf("%d 🐛 [parse_uri] uri: %s\n", __LINE__, uri); // 예) http://localhost:12425/nop-file.txt

  /*============= 👷 1. URI에서 필요한 데이터 추출 =============*/
  // 필요한 데이터 : host, User-Agent, Connection: close, Proxy-Connection: close
  // printf("%d🐛 parse_uri(): %s, %s, %s, %s\n", __LINE__, uri, uri_proxy_to_server, host, port);

  /* http:// 잘라서 host 추출 */
  if (!(ptr = strstr(uri, "://")))
    return -1; // ://가 없으면 invalid uri
  ptr += 3;
  strcpy(host, ptr); // host = localhost:12425/nop-file.txt
  printf("%d 🐛 [parse_uri] host: %s\n", __LINE__, host);

  /* uri_ptos(proxy => server로 보낼 uri 추출 */
  if ((ptr = strstr(host, "/")))
  {
    *ptr = '\0'; // host = localhost:5724
    ptr += 1;
    strcpy(uri_proxy_to_server, "/"); // uri_proxy_to_server = /
    strcat(uri_proxy_to_server, ptr); // uri_proxy_to_server = /nop-file.txt
  }
  else
  {
    strcpy(uri_proxy_to_server, "/");
  }
  printf("%d 🐛 [parse_uri] uri_ptos: %s\n", __LINE__, uri_proxy_to_server);

  /* port 추출 */
  if ((ptr = strstr(host, ":")))
  {              // host = localhost:5724
    *ptr = '\0'; // host = localhost
    ptr += 1;
    strcpy(port, ptr); // port = 5724
  }
  else
  {
    strcpy(port, "80"); // port 없을 경우 "80"을 넣어줌
  }
  printf("%d 🐛 [parse_uri] port: %s\n", __LINE__, port);

  /*
  Parsing 전. Client로부터 받은 Request Line
  => GET http://www.google.com:80/index.html HTTP/1.1

  Pasirng 후.
  => host = www.google.com
  => uri_ptos = /index.html
  => port = 80
  => Server로 보낼 Request Line
  => GET /index.html HTTP/1.1
  */

  return 0; // return for valid check
}