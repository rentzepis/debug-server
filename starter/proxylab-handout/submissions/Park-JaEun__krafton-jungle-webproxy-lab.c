#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LISTEN_PORT "8000"     // 프록시 서버가 사용할 포트 번호

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";


typedef struct cache
{
  char url[MAXLINE];
  char content[MAX_OBJECT_SIZE];
  int content_size;

}cache;

cache caches[MAXLINE];
int cache_cnt;
pthread_mutex_t cache_lock;

void *thread(void *vargp);
void handle_client(int connfd);
int cache_lookup(const char *url, char *content);
void cache_store(const char *url, const char *content, int content_size);


int main() {
  int listenfd, *connfdp;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* 지정된 포트에서 수신 대기 소켓 오픈 */
  listenfd = Open_listenfd(LISTEN_PORT);

  pthread_mutex_init(&cache_lock, NULL); // 캐시 뮤텍스 초기화

  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int)); // 클라이언트 소켓의 포인터를 저장하기 위해 동적 할당
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    /* 새로운 스레드에서 클라이언트 요청 처리 */
    Pthread_create(&tid, NULL, thread, connfdp);
  }
  pthread_mutex_destroy(&cache_lock);

  return 0;
}

/* 스레드 함수: 각 클라이언트 요청을 처리하고 종료 */
void *thread(void *vargp) {
  int connfd = *((int *)vargp);

  Pthread_detach(pthread_self()); // 스레드를 분리하여 독립적으로 실행
  Free(vargp);                    // 메모리 해제
  handle_client(connfd);
  Close(connfd);                  // 클라이언트 소켓 닫기

  return NULL;
}

/* 클라이언트 요청을 처리하여 웹 서버로 전달하고 응답을 다시 클라이언트로 전달 */
void handle_client(int connfd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char cache_content[MAX_OBJECT_SIZE];
  int cache_size;
  rio_t rio;

  /* 클라이언트 소켓에서 데이터를 읽기 위해 robust I/O 초기화 */
  Rio_readinitb(&rio, connfd);

  /* 요청 줄 읽기 및 파싱 */
  if (!Rio_readlineb(&rio, buf, MAXLINE))
    return;
  sscanf(buf, "%s %s %s", method, uri, version);

    /* GET 요청만 처리 */
  if (strcasecmp(method, "GET")) {
    printf("Only GET method is supported.\n");
    return;
  }

    /* 캐시에서 URL 조회 */
  pthread_mutex_lock(&cache_lock);
  if (cache_lookup(uri, cache_content)) {
    Rio_writen(connfd, cache_content, strlen(cache_content)); // 캐시된 콘텐츠 전송
    pthread_mutex_unlock(&cache_lock);

    return;
  }
  pthread_mutex_unlock(&cache_lock);

  /* 캐시에 없으면 웹 서버에 요청 전송 */
  char hostname[MAXLINE], path[MAXLINE], server_response[MAX_OBJECT_SIZE];
  int serverfd, content_length = 0;

  sscanf(uri, "http://%[^/]%s", hostname, path); // URL에서 호스트와 경로 분리

  /* 웹 서버에 연결 */
  serverfd = Open_clientfd(hostname, "80");
  sprintf(buf, "GET %s HTTP/1.0\r\nHost: %s\r\n%s\r\n", path, hostname, user_agent_hdr);
  Rio_writen(serverfd, buf, strlen(buf));

  /* 서버 응답을 클라이언트로 전달 */
  rio_t server_rio;
  Rio_readinitb(&server_rio, serverfd);
  while ((cache_size = Rio_readlineb(&server_rio, server_response, MAX_OBJECT_SIZE)) > 0) {
    Rio_writen(connfd, server_response, cache_size);

    if (content_length + cache_size < MAX_OBJECT_SIZE) {
      strcat(cache_content, server_response); // 캐시할 콘텐츠 누적
      content_length += cache_size;
    }
  }

    /* 응답 종료 후 서버 소켓 닫기 */
  Close(serverfd);

  /* 요청 콘텐츠를 캐시에 저장 */
  if (content_length < MAX_OBJECT_SIZE) {
    pthread_mutex_lock(&cache_lock);
    cache_store(uri, cache_content, content_length);
    pthread_mutex_unlock(&cache_lock);
  }
}

/* 캐시에서 URL에 해당하는 콘텐츠 조회 */
int cache_lookup(const char *url, char *content) {
  for (int i = 0; i < cache_cnt; i++) {
    if (strcmp(caches[i].url, url) == 0) {
      strcpy(content, caches[i].content);

      return 1;
    }
  }
  return 0;
}

/* 캐시에 URL과 콘텐츠 저장 */
void cache_store(const char *url, const char *content, int content_size) {
  if (cache_cnt < MAXLINE && content_size < MAX_OBJECT_SIZE) {
    strcpy(caches[cache_cnt].url, url);
    strcpy(caches[cache_cnt].content, content);
    caches[cache_cnt].content_size = content_size;
    cache_cnt++;
  }
}
