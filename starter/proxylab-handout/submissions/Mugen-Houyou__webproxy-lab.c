/**
 * proxy.c - A concurrent web proxy server based on select
 */
#include "csapp.h"
#include "cache.h"

#define MAX_HEADERS 100
#define HOSTPORT_LEN 262 // 255+6+'\0'
#define SHORT_CHARS 16

typedef struct {
  char method[SHORT_CHARS];
  char uri[MAXLINE];
  char version[SHORT_CHARS];
  char hostname[MAXLINE];
  char port[SHORT_CHARS];
  char path[MAXLINE];

  // 전체 헤더 줄들을 저장 (각 줄은 NULL문자로 끊음!)
  char headers[MAX_HEADERS][MAXLINE];
  int header_count;
} http_request_t;


/* 전역 함수 선언 */
void clienterror(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg );
void client_handler(int connfd);  // Ensure the prototype matches the definition
void sigint_handler(int sig);
void *thread_main_process_client(void *void_arg_p);
void handle_http_request(int clientfd, http_request_t *req);
void tunnel_relay(int clientfd, char *hostname, char *port);

// 이하는 tiny에서 가져온 파트
int parse_uri(char* uri, char* hostname, char* port, char* path);


/* 전역 변수 */
// int g_total_bytes_received = 0; 
static cache_t* g_shared_cache = NULL;


/* $begin proxyserversmain */
int main(int argc, char **argv){
  int listenfd;//, connfd, port; 
  socklen_t clientlen = sizeof(struct sockaddr_in);
  struct sockaddr_in clientaddr;
  
  g_shared_cache = Malloc(sizeof(cache_t));
  cache_init(g_shared_cache);
  signal(SIGINT, sigint_handler); // 시그널 핸들러는 가능한 빨리

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    int* connfd_p = Malloc(sizeof(int));
    *connfd_p = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    pthread_t tid;
    pthread_create(&tid, NULL, thread_main_process_client, connfd_p);
  }
}
/* $end proxyserversmain */

void sigint_handler(int sig) {
  cache_deinit(g_shared_cache);
  Free(g_shared_cache);
  g_shared_cache = NULL;
  printf("cache_deinit() 및 Free() 완료. Bye!\n");
  exit(0);
}

void *thread_main_process_client(void *void_arg_p) {
  int connfd = *((int *)void_arg_p);

  Free(void_arg_p); // 얘는 받자마자 free시킴.
  void_arg_p = NULL;
  pthread_detach(pthread_self());  // 스레드 종료 시 자동 회수

  client_handler(connfd);  // 클라이언트 요청 처리

  Close(connfd);
  return NULL;
}

void client_handler(int connfd){
  rio_t client_rio;
  char buf[MAXLINE], line[MAXLINE];
  http_request_t* req_p = Malloc(sizeof(http_request_t));

  Rio_readinitb(&client_rio, connfd);

  // 요청 라인 읽기
  if (Rio_readlineb(&client_rio, buf, MAXLINE) <= 0)  // EOF면 연결 정리 
    return;
  // sscanf(buf, "%s %s %s", req_p->method, req_p->uri, req_p->version);
  sscanf(buf, "%s %s %s", req_p->method, req_p->uri, req_p->version);

  // CONNECT일 경우 터널링 (양방향 TCP 패스쓰루)
  if (!strcasecmp(req_p->method, "CONNECT")) {
    char *colon = strchr(req_p->uri, ':');
    if (colon) {
      *colon = '\0';
      strcpy(req_p->hostname, req_p->uri);
      strcpy(req_p->port, colon + 1);
    } else {
      strcpy(req_p->hostname, req_p->uri);
      strcpy(req_p->port, "443");
    }
    tunnel_relay(connfd, req_p->hostname, req_p->port);
    return;
  }

  // 일반 HTTP 요청 처리 
  if (!parse_uri(req_p->uri, req_p->hostname, req_p->port, req_p->path)) {
    clienterror(connfd, req_p->uri, "400", "Bad Request", "URI 파싱 실패");
    return;
  }

  // 나머지 헤더 수집 
  req_p->header_count = 0;
  while (Rio_readlineb(&client_rio, line, MAXLINE) > 0) {
    if (!strcmp(line, "\r\n")) break;
    if (req_p->header_count < MAX_HEADERS)
      strcpy(req_p->headers[req_p->header_count++], line);
  }

  handle_http_request(connfd, req_p);
  Free(req_p);
  req_p = NULL;
}

void handle_http_request(int clientfd, http_request_t *req) {
  /**
    1. Open_clientfd(hostname, port)
    2. write(서버에 요청 라인 + 헤더)
    3. read(서버 응답)
    4. write(응답을 clientfd로 전달)
    5. Close(serverfd)
   */
  int serverfd;
  char buf[MAXLINE];

  int cached_size;
  char *cached_buf = Malloc(MAX_OBJECT_SIZE);
  
  // 캐시 있을 때
  if (cache_get(g_shared_cache, req->uri, cached_buf, &cached_size)) {
    Rio_writen(clientfd, cached_buf, cached_size);
    Free(cached_buf);
    cached_buf = NULL;
    return;  // 캐시 히트! 얼리 리턴.
  }
  // 아래부터는 전부 캐시 없을 때

  serverfd = Open_clientfd(req->hostname, req->port);
  if (serverfd < 0) {
    clienterror(clientfd, req->hostname, "502", "Bad Gateway", "Proxy couldn't connect to origin server");
    return;
  }
  
  // http://httpforever.com/js/init.min.js, httpforever.com, 80, /js/init.min.js
  // printf("%s, %s, %s, %s\n",req->uri, req->hostname, req->port, req->path);

  // 요청 라인
  snprintf(buf, MAXLINE, "%s %s HTTP/1.0\r\n", req->method, req->path);
  Rio_writen(serverfd, buf, strlen(buf));

  // 헤더 전송
  int has_host = 0;
  for (int i = 0; i < req->header_count; ++i) {
    if (strncasecmp(req->headers[i], "Host:", 5) == 0)
      has_host = 1;
    else if (strncasecmp(req->headers[i], "Connection:", 11) == 0)
      continue;
    else if (strncasecmp(req->headers[i], "Proxy-Connection:", 17) == 0)
      continue;
    else if (strncasecmp(req->headers[i], "User-Agent:", 11) == 0)
      continue;

    Rio_writen(serverfd, req->headers[i], strlen(req->headers[i]));
  }

  // 헤더 보완
  if (!has_host) {
    snprintf(buf, MAXLINE, "Host: %s\r\n", req->hostname);
    Rio_writen(serverfd, (void*)buf, strlen(buf));
  }

  // 필수 헤더들 통일
  sprintf(buf, "Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "Proxy-Connection: close\r\n");
  Rio_writen(serverfd, buf, strlen(buf));
  sprintf(buf, "User-Agent: Mozilla/5.0 (compatible; GabesProxy/1.0)\r\n");
  Rio_writen(serverfd, buf, strlen(buf));

  // 클라이언트로부터의 리퀘스트를 서버로 전달 끝.
  // 아래부터는 서버로부터의 리스폰스를 클라이언트에 전달.
  Rio_writen(serverfd, "\r\n", 2);

  // 리스폰스 중계 & 캐시 저장
  char *resp_buf = Malloc(MAX_OBJECT_SIZE);
  char *object_buf = Malloc(MAX_OBJECT_SIZE);
  int object_size = 0;
  rio_t server_rio;
  ssize_t n;

  Rio_readinitb(&server_rio, serverfd);
  while ((n = Rio_readlineb(&server_rio, resp_buf, MAXLINE)) > 0){
    if (object_size + n <= MAX_OBJECT_SIZE)
      memcpy(object_buf + object_size, resp_buf, n);
      object_size += n;
    Rio_writen(clientfd, resp_buf, n);
  }

  // 3. 리스폰스 헤더 && 보디를 통째로 캐시로 저장
  if (object_size <= MAX_OBJECT_SIZE)
    cache_put(g_shared_cache, req->uri, object_buf, object_size);

  Free(object_buf);
  Free(cached_buf);
  object_buf = NULL;
  cached_buf = NULL;

  Close(serverfd);
}

/**
 * parse_uri - 프록시 요청 파싱. 
 * 첫 줄 예시: GET http://www.example.com/asdf/index.html HTTP/1.0
 */
/* strstr(): strstr() 함수는 string1에서 string2의 첫 번째 표시를 찾습니다. 
      함수는 일치 프로세스에서 string2로 끝나는 NULL자(\0)를 무시합니다. 
*/
int parse_uri(char* uri, char* hostname, char* port, char* path) {
  char* host_p;
  char* path_p;
  char* port_p;
  char hostport[HOSTPORT_LEN]; // hostname+port 임시 저장

  // http:// 접두어 확인
  if (strncasecmp(uri, "http://", 7) != 0)
    return 0;

  host_p = uri + 7;  // "http://" 이후부터

  // path 위치 찾기
  path_p = strchr(host_p, '/');
  if (path_p != NULL) { // strchr() 결과가 NULL일 수 있으므로
    strcpy(path, path_p);   // "/foo/bar.html"
    
    size_t len = path_p - host_p; // hostname:port만 남기기 위해 임시로 NULL 종결

    if (len >= SHORT_CHARS) 
      return 0;  // 길이 초과 방지

    memcpy(hostport, host_p, len);
    hostport[len] = '\0';

  } else { // http://example.com 같은 형태
    strcpy(path, "/"); 
    strcpy(hostport, host_p); 
  }

  // 포트 파싱
  port_p = strchr(hostport, ':');
  if (port_p != NULL) {
    *(char*) port_p = '\0';  // hostname 분리
    strcpy(hostname, hostport);
    strcpy(port, port_p + 1);
  } else {
    strcpy(hostname, hostport);
    strcpy(port, "80");  // 기본 포트
  }

  return 1;
}

void clienterror(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg ){
  char buf[MAXLINE], body[MAXBUF];
  
  // HTTP response body를 만듦
  /* 이때, printf vs. fprintf vs. sprintf?
    sprintf는 파일이나 화면이 아니라 변수(버퍼)에 문자열을 출력한다 (담는다).
  */
  sprintf(body, "<html><title>Gabe_s Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>Gabe_s web proxy server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf)); // 클라이언트의 소켓에 전송. 클라이언트는 여기서부터 실제 HTML 콘텐츠를 렌더링하게 됨.
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/**
 * 터널링: 클라이언트 - 프록시 - 오리진 서버 (양방향 TCP 패스쓰루) 
 * UDP는 나도 모르겠다.
 */
void tunnel_relay(int clientfd, char *hostname, char *port){
    int serverfd;
    int maxfd;
    fd_set readset;
    ssize_t n;
    char buf[MAXBUF];

    /* 오리진 서버로 TCP 연결 */
    if ((serverfd = Open_clientfd(hostname, port)) < 0) {
      clienterror(clientfd, hostname, "502", "Bad Gateway", "Unable to connect to the origin server");
      return;
    }
    
    /* 여기서 200 OK 응답을 먼저 클라이언트에게 보내야 함 */
    const char *okmsg = "HTTP/1.0 200 Connection Established\r\n\r\n";
    Rio_writen(clientfd, (void*) okmsg, strlen(okmsg));
    
    /* 양쪽 소켓을 select()로 감시하며 한쪽에서 읽어 다른쪽으로 */
    maxfd = (clientfd > serverfd ? clientfd : serverfd) + 1;

    while (1) {
      FD_ZERO(&readset);
      FD_SET(clientfd, &readset);   /* 클라이언트 → 서버 방향 감시 */
      FD_SET(serverfd, &readset);   /* 서버 → 클라이언트 방향 감시 */

      if (Select(maxfd, &readset, NULL, NULL, NULL) < 0)
        break;          /* select 에러면 터널 종료 */

      /* 클라이언트로부터 데이터 도착 */
      if (FD_ISSET(clientfd, &readset)) {
        n = read(clientfd, buf, sizeof(buf));
        if (n <= 0) 
          break;        /* EOF 또는 에러 → 터널 닫기 */
        Rio_writen(serverfd, buf, n);
      }

      /* 오리진 서버로부터 데이터 도착 */
      if (FD_ISSET(serverfd, &readset)) {
        n = read(serverfd, buf, sizeof(buf));
        if (n <= 0)
          break;        /* EOF 또는 에러 → 터널 닫기 */
        Rio_writen(clientfd, buf, n);
      }
    }

    Close(serverfd);
}


// 폐기
void client_handler_stack_version(int connfd){
  rio_t client_rio;
  char buf[MAXLINE], line[MAXLINE];
  http_request_t req;

  Rio_readinitb(&client_rio, connfd);

  // 요청 라인 읽기
  if (Rio_readlineb(&client_rio, buf, MAXLINE) <= 0)  // EOF면 연결 정리 
    return;
  sscanf(buf, "%s %s %s", req.method, req.uri, req.version);

  // CONNECT일 경우 터널링 (양방향 TCP 패스쓰루)
  if (!strcasecmp(req.method, "CONNECT")) {
    char *colon = strchr(req.uri, ':');
    if (colon) {
      *colon = '\0';
      strcpy(req.hostname, req.uri);
      strcpy(req.port, colon + 1);
    } else {
      strcpy(req.hostname, req.uri);
      strcpy(req.port, "443");
    }
    tunnel_relay(connfd, req.hostname, req.port);
    return;
  }

  // 일반 HTTP 요청 처리 
  if (!parse_uri(req.uri, req.hostname, req.port, req.path)) {
    clienterror(connfd, req.uri, "400", "Bad Request", "URI 파싱 실패");
    return;
  }

  // 나머지 헤더 수집 
  req.header_count = 0;
  while (Rio_readlineb(&client_rio, line, MAXLINE) > 0) {
    if (!strcmp(line, "\r\n")) break;
    if (req.header_count < MAX_HEADERS)
      strcpy(req.headers[req.header_count++], line);
  }
  handle_http_request(connfd, &req);
}