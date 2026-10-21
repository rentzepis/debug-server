#include <stdio.h>
#include "csapp.h"
#include <signal.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// gcc -o proxy proxy.c csapp.c

/*캐시 구조체*/
typedef struct cache_t {
  char host[MAXLINE]; /* 저장된 호스트명 */         
  char port[MAXLINE]; /* 저장된 포트 */
  char path[MAXLINE]; /* 저장된 경로*/
  char object[MAX_OBJECT_SIZE]; /* 저장된 객체 */
  int size; /* 저장된 객체의 크기*/
  struct cache_t *prev;       /* 이전 위치 */
  struct cache_t *next;       /* 다음 위치 */
} cache_t;

cache_t *start; /* 캐시 메모리 시작지점 */
cache_t *end; /* 캐시 메모리 끝지점 */
int cache_size = 0; /* 저장된 캐시 크기 */

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *host, char *port, char *path, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void send_get_request_from_server(int clientfd, char *hostname, char *path, char *method);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, *connfdp; // 듣기 & 연결 파일 디스크럽터, 
  char hostname[MAXLINE], port[MAXLINE]; // 호스트명, 포트

  struct sockaddr_storage clientaddr; // 클라이언트 주소
  socklen_t clientlen; // 클라이언트 길이

  pthread_t tid; // 쓰레드 식별자
  
  signal(SIGPIPE, SIG_IGN); // SIGPIPE 신호 무시

  /* 커멘드 라인에 포트번호 입력되었는지 확인 */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /*캐시 시작점 초기화*/
  start = malloc(sizeof(cache_t));
  end = malloc(sizeof(cache_t));
  start->next = end;
  end->prev = start;

  /* 듣기 파일디스크럽터 준비 */
  listenfd = Open_listenfd(argv[1]);

  while (1) {
    /* 연결 파일디스크럽터 준비 */
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    /* 연결 정보 불러오기 */
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    /* 쓰레드 생성 */
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}

/* 쓰레드 루틴*/
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/* 실제 작업이 일어나는 함수 */
void doit(int fd) {
  int is_static, is_head, client_fd;
  struct stat sbuf;
  char buf_from[MAXLINE], buf_to[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], path[MAXLINE], cgiargs[MAXLINE];
  rio_t rio_from, rio_to;

  /* request line과 header를 읽는다. */
  rio_readinitb(&rio_from, fd);
  Rio_readlineb(&rio_from, buf_from, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf_from);
  sscanf(buf_from, "%s %s %s", method, uri, version);

  /* GET 메서드가 아닌 경우 501 에러 */
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  /* GET 요청으로부터 URI를 파싱한다. */
  parse_uri(uri, host, port, path, cgiargs);

  /* 캐시에 있는지 확인 */
  /* 있으면 가져온다 */
  cache_t *find_pos = start->next;
    while (find_pos != end) {
      // host, port, path 모두 일치할 경우
      if( !strcmp(host, find_pos->host) && !strcmp(port, find_pos->port) && !strcmp(path, find_pos->path) ) {
        // fd에 캐시 데이터를 쓴다.
        Rio_writen(fd, find_pos->object, find_pos->size);

        // 캐시 링크드 리스트 위치를 재정립한다.
        find_pos->prev->next = find_pos->next;
        find_pos->next->prev = find_pos->prev;

        start->next->prev = find_pos;
        find_pos->next = start->next;

        start->next = find_pos;
        find_pos->prev = start;
        return;
      } else {
        find_pos = find_pos->next;
      }
    }

  /* 못찾은경우 찾아야하는 서버와 연결을 수립한다. */
  client_fd = open_clientfd(host, port);
  /* 연결 못한경우 doit 종료*/
  if(client_fd < 0)
    return;
  sprintf(buf_to, "GET %s HTTP/1.0\r\n\r\n", path); // HTTP 요청 작성
  Rio_writen(client_fd, buf_to, strlen(buf_to)); // 서버로 요청 전송

  int n; // 방금 서버에서 읽어온 크기
  int now_size = 0; // 여태까지 서버에서 읽어온 크기
  char temp_object[MAX_OBJECT_SIZE];  // 캐시에 저장할 임시 객체
  int size_flag = 1; // 0일 경우 사이즈 초과라는 의미

  /* 읽은 만큼 fd에 쓴다. */
  while ((n = Rio_readn(client_fd, buf_from, MAXLINE)) > 0) {
    Rio_writen(fd, buf_from, n);

    // 크기 초과 시, flag가 바뀌고 반복문이 끝난다.
    if(now_size + n > MAX_OBJECT_SIZE) {
      size_flag = 0;
      break;
    }
    memcpy(temp_object + now_size, buf_from, n);  // 임시 객체에 데이터 누적 저장한다.
    now_size += n;
  }
  
  /*캐시 저장 로직*/
  if(size_flag) {
    // 초과시 삭제한다.
    while( cache_size + now_size > MAX_CACHE_SIZE ) {
      cache_t *del_pos = end->prev;
      if (del_pos != start) { // 시작점이 아닐 경우에만 수행한다..
        cache_size -= del_pos->size;
        del_pos->prev->next = end;
        end->prev = del_pos->prev;
        free(del_pos);
      }
    }
    
    // 새로운 캐시 데이터를 삽입한다.
    cache_t *new_cache = malloc(sizeof(cache_t));
    strcpy(new_cache->host, host);
    strcpy(new_cache->port, port);
    strcpy(new_cache->path, path);

    memcpy(new_cache->object, temp_object, now_size);  // 임시 객체 내용을 캐시로 복사한다.
    new_cache->size = now_size;

    // 위치를 맨 앞으로 넣는다.

    start->next->prev = new_cache;
    new_cache->next = start->next;

    start->next = new_cache;
    new_cache->prev = start;

    cache_size += now_size;
  }

  Close(client_fd); // 서버와 연결 종료한다.
}

/* 클라이언트 에러 메시지를 만든다. */
void clienterror(int fd, char *cause, char*errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* HTTP Response Body를 만든다.*/
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* HTTP response 를 출력한다. */
  sprintf(buf, "HTTP/1.0 %s %s\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

/* uri로 host, port, path, cgiargs를 만든다. */
int parse_uri(char *uri, char *host, char *port, char *path, char *cgiargs) {
  char *slash_pos;
  int ret;

  /*http, https 제거*/
  if( strncmp(uri, "http://", 7) == 0 ) {
    memmove(uri, uri + 7, strlen(uri) - 7 + 1);
  } else if( strncmp(uri, "https://", 8) == 0 ) {
    memmove(uri, uri + 8, strlen(uri) - 8 + 1);
  } 

  /* cgiargs를 추출한다. */
  slash_pos = strchr(uri, '&'); // '&' 문자의 위치
  if (slash_pos != NULL) {
    strcpy(cgiargs, slash_pos);
    *slash_pos = '\0';
    ret = 1;
  } else {
    strcpy(cgiargs, "");
    ret = 0;
  }

  /* path를 추출한다. */
  slash_pos = strchr(uri, '/'); // '/' 문자의 위치
  if (slash_pos != NULL) {
    strcpy(path, slash_pos);
    *slash_pos = '\0';
  } else {
    strcpy(path, "/");
  }
  
  /* port를 추출한다. */
  slash_pos = strchr(uri, ':'); // ':' 문자의 위치
  if (slash_pos != NULL) {
    strcpy(port, slash_pos+1);
    *slash_pos = '\0';
  } else {
    strcpy(port, "80");
  }

  /* 남은 것이 host이다. */
  strcpy(host, uri);

  return ret;
}