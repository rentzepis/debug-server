#include <stdio.h>
#include "csapp.h"
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache
{
  char uri[MAXLINE], hostname[MAXLINE];
  int response_size;
  char *response_ptr;
  struct cache *prev, *next;
} cache;

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *fd, char *request_buf, char *hostname, char *port);
int parse_uri(char *uri, char *filename, char *hostname, char *port);
void *thread(void *vargp);
cache *search_cache(char *uri);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

cache *rootp = NULL, *tailp = NULL;

unsigned int current_size = 0;
char cache_buf[MAX_CACHE_SIZE];

int main(int argc, char **argv)
{
  int listenfd, *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr,
                     &clientlen); // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfd);
  }
}

/* 피어 쓰레드 루틴 */
void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int ctopfd)
{
  char *ptosfd;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], hostname[MAXLINE], port[MAXLINE], request_buf[MAXLINE];
  int content_length;
  rio_t client_rio, server_rio; // 클라이언트와 통신하는 리오, 엔드 서버와 통신하는 리오

  /*Read request line and headers*/
  Rio_readinitb(&client_rio, ctopfd);
  Rio_readlineb(&client_rio, buf, MAXLINE); // 요청 첫줄 읽기
  sscanf(buf, "%s %s %s", method, uri, version);
  /* 파비콘 차단 */
  if (!strcasecmp(uri, "/favicon.ico"))
  {
    return;
  }
  parse_uri(uri, filename, hostname, port);

  // 클라이언트로부터 정보 다 받아왔으니 해당 정보로 캐시 조회
  if (rootp) // 캐시가 비어있지 않은 경우에만
  {
    // uri로 알맞은 캐시 찾기
    cache *c = search_cache(uri);
    if (c)
    {
      // 캐시 안의 데이터 클라이언트에게 보내주기
      Rio_writen(ctopfd, c->response_ptr, c->response_size);
      if (c != rootp)
      {
        connect_prev_next(c); // 캐시 리스트 앞뒤를 서로 이어주기
        connect_root(c);  // 캐시 리스트 가장 앞으로 이동 (lru를 위해)
      }
      return;
    }
  }

  /* =============== 엔드 서버랑 통신 ================== */ 

  ptosfd = open_clientfd(hostname, port); // 프록시 <-> 서버 소켓 오픈

  if(ptosfd < 0){
    return;
  } 

  // 요청 첫줄 보내주기
  sprintf(request_buf, "%s %s %s\r\n", method, filename, "HTTP/1.0");
  Rio_writen(ptosfd, request_buf, strlen(request_buf));

  read_requesthdrs(&client_rio, ptosfd, request_buf, hostname, port); // 클라이언트로부터 받은 헤더 엔드서버로 전송

  // 프록시 <-> 엔드서버, 엔드서버에서 온 리스폰스 받아주기
  char response_buf[MAX_OBJECT_SIZE];
  ssize_t response_size = Rio_readn(ptosfd, response_buf, MAX_OBJECT_SIZE); // 리스폰스 전체 사이즈
  Close(ptosfd);

  // 클라이언트 <-> 프록시,  프록시가 받은 리스폰스 클라이언트한테 보내주기 
  Rio_writen(ctopfd, response_buf, response_size);
  // 현재까지 총 캐시사이즈 + 지금 처리할 데이터 사이즈 > 맥스 캐시 사이즈 인 경우
  while (current_size + response_size > MAX_CACHE_SIZE)
  {
    if (response_size > MAX_CACHE_SIZE) // 그냥 맥스사이즈보다 큰경우는 캐싱 안함
    {
      return;
    }
    tailp = tailp->prev;
    free(tailp->next);
    tailp->next = NULL;
  }

  // 캐시 처리
  char *response_ptr;
  cache *new_cache;
  new_cache = (cache *)calloc(1, sizeof(cache));
  new_cache->response_size = response_size;
  strcpy(new_cache->uri, uri);

  response_ptr = (char *)calloc(1, response_size);
  current_size += response_size;
  memcpy(response_ptr, response_buf, response_size);
  new_cache->response_ptr = response_ptr;
  connect_root(new_cache);
}

// 헤더를 받아서 보내주는 함수(rp를 읽어서 fd에 보내줌)
void read_requesthdrs(rio_t *rp, char *fd, char *request_buf, char *hostname, char *port)
{
  int is_host_exist;
  int is_connection_exist;
  int is_proxy_connection_exist;
  int is_user_agent_exist;

  Rio_readlineb(rp, request_buf, MAXLINE);
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

    Rio_writen(fd, request_buf, strlen(request_buf)); 
    Rio_readlineb(rp, request_buf, MAXLINE);          
  }

  // 필수 헤더 없으면 만들어서 보내주기
  if (!is_proxy_connection_exist)
  {
    sprintf(request_buf, "Proxy-Connection: close\r\n");
    Rio_writen(fd, request_buf, strlen(request_buf));
  }
  if (!is_connection_exist)
  {
    sprintf(request_buf, "Connection: close\r\n");
    Rio_writen(fd, request_buf, strlen(request_buf));
  }
  if (!is_host_exist)
  {
    sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
    Rio_writen(fd, request_buf, strlen(request_buf));
  }
  if (!is_user_agent_exist)
  {
    sprintf(request_buf, user_agent_hdr);
    Rio_writen(fd, request_buf, strlen(request_buf));
  }

  sprintf(request_buf, "\r\n"); 
  Rio_writen(fd, request_buf, strlen(request_buf));
  return;
}

/* 파싱해주기 */
int parse_uri(char *uri, char *filename, char *hostname, char *port)
{
  char *start_idx = NULL, *port_idx = NULL, *path_idx = NULL;

  strcpy(filename, "/");
  if (*uri == '/')
  {
    uri += 1;
  }
  if (start_idx = strstr(uri, "//"))
    uri = start_idx + 2;

  if (port_idx = strchr(uri, ':'))
  {
    *port_idx = '\0';
    if (path_idx = strchr(port_idx + 1, '/'))
    {
      *path_idx = '\0';
      strcat(filename, path_idx + 1);
    }
    strcpy(port, port_idx + 1);
  }
  strcpy(hostname, uri);
  if (port_idx == NULL)
    strcpy(port, "80");
  else
    *port_idx = ':';
  *path_idx = '/';

  return 0;
}

/* 캐시 찾기 */
cache *search_cache(char *uri)
{
  cache *current = rootp;
  while (current)
  {
    if (strcasecmp(current->uri, uri) == 0)
    {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

/* lru에 맞게 캐시 가장 앞으로 댕겨주기 */
void connect_root(cache *c)
{
  if (!rootp)
  {
    rootp = c;
    tailp = c;
    return;
  }
  if (c == tailp)
  {
    tailp = c->prev;
    c->prev->next = NULL;
  }
  c->prev = NULL;
  c->next = rootp;
  rootp->prev = c;
  rootp = c;
}

/* 자신은 빼고 앞뒤 캐시 블록 연결해주기 */
void connect_prev_next(cache *c)
{
  c->prev->next = c->next;
  if (c->next)
  {
    c->next->prev = c->prev;
  }
}
