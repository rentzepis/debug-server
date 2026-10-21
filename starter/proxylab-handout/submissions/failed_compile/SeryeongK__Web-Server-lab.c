#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define LRU_MAGIC_NUMBER 10
#define CACHE_OBJS_COUNT 10 /* 저장되는 캐시의 최댓값 */

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *new_version = "HTTP/1.0";

void do_it(int fd);
void do_request(int serverfd, char *method, char *uri_ptos, char *host);
int parse_uri(char *uri, char *uri_ptos, char *host, char *port);
int parse_responsehdrs(rio_t *rp, int length);
void *thread(int vargp);
/*cache function*/
void cache_init();
int cache_find(char *url);
int cache_eviction();
void cache_LRU(int index);
void cache_uri(char *uri, char *buf);
void readerPre(int i);
void readerAfter(int i);

/*
파일 디스크립터: 컴퓨터 프로그램이 파일 또는 기타 입/출력 리소스를 참조하는 방법
책에 대한 도서관의 참조 번호와 같음. 리소스는 일반적으로 운영 체제에서 관리함.

rio_t 구조체는 Robust I/O(Rio)라이브러리에서 사용되는 데이터 구조체

typedef struct {
    int rio_fd;                 // 파일 디스크립터
    ssize_t rio_cnt;            // 남아 있는 데이터의 바이트 수
    char *rio_bufptr;           // 다음에 읽어들일 데이터의 위치
    char rio_buf[RIO_BUFSIZE];  // 내부 버퍼
} rio_t;
*/

typedef struct
{
  char cache_obj[MAX_OBJECT_SIZE]; /* 캐시 객체 */
  char cache_url[MAXLINE];         /* 캐시 URL */
  int LRU;                         /* LRU 값 */
  int isEmpty;                     /* 해당 캐시 블록이 비어있는지 나타내는 플래그 */

  int readCnt;      /* 캐시 블록에 접근하는 동안 읽는 쓰레드의 수 */
  sem_t wmutex;     /* 캐시에 대한 접근 */
  sem_t rdcntmutex; /* readCnt에 대한 접근을 보호하기 위한 세마포어 */

} cache_block; /* 캐시 블록 하나 */

typedef struct
{
  cache_block cacheobjs[CACHE_OBJS_COUNT]; /* 캐시 블록을 저장하는 배열 / CACHE_OBJS_COUNT: 프록시 서버에서 사용되는 캐시 블록의 수(10개) */
  int cache_num;                           /* 현재 캐시에 저장되어 있는 캐시 객체의 개수 */
} Cache;

Cache cache;

int main(int argc, char **argv)
{
  int listenfd, *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  cache_init();

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN); /* SIGPIPE: 프로세스가 다른 쪽 끝에서 닫힌 소켓이나 파이프에 쓰려고 할 때
                               신호를 처리하지 않으면 프로세스를 종료 */

  listenfd = Open_listenfd(argv[1]);

  /* 🧠 개선점 => 쓰레드 풀(워커 쓰레드)을 미리 만들어서 client가 들어오면 작업 할당(큐) */

  /*
  다른 방법 - 만약에 connfd가 큰 메모리 공간을 가지는 변수라면 비효율적일 수 있음
  🤔 malloc을 사용하지 않고 포인터로 바로 넘겨줄 경우는?
  connfd를 여러 개의 쓰레드가 참조할 경우 이전 쓰레드의 동작이 끝나기 전에
  connfd가 변경될 수 있음 => 값이 변경되어 문제가 생김
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfd);
  }
  */

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s %s).\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfd); /* 굳이 포인터로 넘겨줄 필요가 없음 */
  }

  return 0;
}

void do_it(int p_connfd)
{
  int serverfd;
  char buf[MAXLINE], host[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; /* 프록시가 요청을 보낼 서버의 IP, port */
  char uri_ptos[MAXLINE];
  rio_t rio;

  /*
  Rio_readinitb: rio_t 구조체를 초기화하는 함수
  serve_fd라는 파일 디스크립터를 사용하여 rio_t 구조체인 rio 초기화
  */
  Rio_readinitb(&rio, p_connfd); /* 클라이언트와의 connectoin */
  /*
  Rio_readlineb: 소켓을 통해 데이터를 읽는 함수
  rio_t 구조체 변수 / 응답 메시지를 저장하기 위한 버퍼 / 버퍼의 크기
  */
  Rio_readlineb(&rio, buf, MAXLINE); /* 클라이언트의 요청 읽기 */
  printf("Request headers to proxy:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); /* 클라이언트의 요청 파싱 => method, uri, version */

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {
    /* 클라이언트의 요청이 GET이나 HEAD가 아니면 501 에러 */
    printf("[PROXY] 501 ERROR\n");
    return;
  }

  /* the uri is cached ? */
  int cache_index;
  if ((cache_index = cache_find(uri)) != -1)
  { /* uri가 cache에 있으면 */
    readerPre(cache_index);
    /*
    Rio_writen: 소켓을 통해 데이터를 씀
    소켓 파일 디스크립터(데이터를 보낼 대상) / HTTP 요청 메시지를 가리키는 포인터(보낼 데이터)) / HTTP 요청 메시지의 길이(보낼 데이터의 크기)
    */
    Rio_writen(p_connfd, cache.cacheobjs[cache_index].cache_obj, strlen(cache.cacheobjs[cache_index].cache_obj)); /* 캐시 객체 보내기 */
    readerAfter(cache_index);

    /* 읽은 값의 LRU를 업데이트 해주기 */
    writePre(cache_index);
    if (cache.cacheobjs[cache_index].isEmpty == 0) /* 캐시 블록이 비어있을 때 */
    {
      cache.cacheobjs[cache_index].LRU = LRU_MAGIC_NUMBER; /* LRU 숫자 늘리기 */
    }
    writeAfter(cache_index);
    return;
  }

  parse_uri(uri, uri_ptos, host, port); /* uri 파싱 => path, hostname, path, port에 할당 */
  /* ====== end server에 요청 보낼 준비 완료! ====== */

  /* hostname, port 서버에 대한 connection 열기 => 서버와의 소켓 디스크립터 생성 */
  serverfd = Open_clientfd(host, port);
  do_request(serverfd, method, uri_ptos, host);
  /* ====== end server에 요청 완료! ====== */
  /* 기존의 do_response */
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0;
  size_t n;
  Rio_readinitb(&rio, serverfd); /* 서버 소켓과 연결 */

  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) /* 읽을 데이터가 없을 때까지 반복, rio에서 한 줄씩 읽고 buf에 저장 */
  {
    sizebuf += n;                  /* sizebuf에 전체 데이터의 크기가 저장됨 */
    if (sizebuf < MAX_OBJECT_SIZE) /* 크기가 허용되는 최대 객체 크기보다 작은지 확인 */
      strcat(cachebuf, buf);       /* cachebuf에 buf를 추가 */
    Rio_writen(p_connfd, buf, n);  /* 클라이언트에 buf를 보냄 */
  }
  /* ===== end server에서 받은 응답 => 클라이언트에게 전송 완료! =====*/
  Close(serverfd); /* 서버와의 연결 종료 */

  /*store it*/
  if (sizebuf < MAX_OBJECT_SIZE)
  {
    cache_uri(uri, cachebuf);
  }
}

void do_request(int serverfd, char *method, char *uri_ptos, char *host)
{
  char buf[MAXLINE];
  printf("Request headers to server: \n");
  printf("%s %s %s\n", method, uri_ptos, new_version);

  sprintf(buf, "GET %s %s\r\n", uri_ptos, new_version);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnections: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n", buf);

  Rio_writen(serverfd, buf, (size_t)strlen(buf)); /* 서버에 HTTP request 메시지를 보냄 */
}

int parse_uri(char *uri, char *uri_ptos, char *host, char *port)
{
  char *ptr;

  if (!(ptr = strstr(uri, "://")))
    return -1;
  ptr += 3;
  strcpy(host, ptr);

  if ((ptr = strchr(host, ':'))) /* strchr(): 문자 하나만 찾는 함수 (''작은따옴표사용) */
  {
    *ptr = '\0';
    ptr += 1;
    strcpy(port, ptr);
  }
  else
  {
    if ((ptr = strchr(host, '/')))
    {
      *ptr = '\0';
      ptr += 1;
    }
    strcpy(port, "80");
  }

  if ((ptr = strchr(port, '/')))
  {
    *ptr = '\0';
    ptr += 1;
    strcpy(uri_ptos, "/");
    strcat(uri_ptos, ptr);
  }
  else
    strcpy(uri_ptos, "/");

  return 0;
}

/* Thread routine */
/* 다른 방법
void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  do_it(connfd);
  Close(connfd);
}
*/

void *thread(int connfd)
{
  Pthread_detach(pthread_self());
  do_it(connfd);
  Close(connfd); /* 클라이언트와의 연결 종료 */
}

/**************************************
 * Cache Function
 **************************************/

void cache_init()
{
  cache.cache_num = 0;
  int i;
  for (i = 0; i < CACHE_OBJS_COUNT; i++) /* 10개의 블록 반복 */
  {
    cache.cacheobjs[i].LRU = 0;
    cache.cacheobjs[i].isEmpty = 1;
    cache.cacheobjs[i].readCnt = 0;
    Sem_init(&cache.cacheobjs[i].wmutex, 0, 1);     /* wmutex를 1로 초기화 */
    Sem_init(&cache.cacheobjs[i].rdcntmutex, 0, 1); /* rdcntmutex를 1로 초기화
                                                       🤔 왜? 한 번에 하나의 쓰레드만 readCnt 변수를 수정할 수 있도록 하기 위해서 */
  }
}

/* 읽기 전에 */
void readerPre(int i)
{
  P(&cache.cacheobjs[i].rdcntmutex);   /* 하나가 읽게 됨 => rdcntmutex 감소(0이 됨. 잠금)
                                          잠그는 이유는 readCnt에 대한 접근을 막기 위해서(readCnt에서의 경합 상태 방지) */
  cache.cacheobjs[i].readCnt++;        /* 읽는 접근에 1 추가 */
  if (cache.cacheobjs[i].readCnt == 1) /* 읽고 있는 쓰레드가 있으면 */
    P(&cache.cacheobjs[i].wmutex);     /* wmutex 감소 => 0이 됨. 잠금 */
  V(&cache.cacheobjs[i].rdcntmutex);   /* rdcntmutex 증가 => 1이 됨. 잠금 해제 */
}

/* 읽고 난 후에 */
void readerAfter(int i)
{
  P(&cache.cacheobjs[i].rdcntmutex);   /* rdcntmutext 감소(0이 됨. 잠금) */
  cache.cacheobjs[i].readCnt--;        /* 읽는 접근에 1 감소(아무도 안 읽는 상태) */
  if (cache.cacheobjs[i].readCnt == 0) /* 읽고 있는 쓰레드가 없으면 */
    V(&cache.cacheobjs[i].wmutex);     /* wmutex 증가 => 1이 됨. 잠금 해제 */
  V(&cache.cacheobjs[i].rdcntmutex);   /* rdcntmutex 증가 => 1이 됨. 잠금 해제 */
}

void writePre(int i)
{
  P(&cache.cacheobjs[i].wmutex); /* wmutex 감소 => 0이 됨. 잠금*/
}

void writeAfter(int i)
{
  V(&cache.cacheobjs[i].wmutex); /* wmutex 증가 => 1이 됨. 잠금 해제 */
}

/* url이 캐시에 있는지 찾기 */
int cache_find(char *url)
{
  int i;
  for (i = 0; i < CACHE_OBJS_COUNT; i++) /* 10개의 캐시 블록을 전부 돌면서 */
  {
    readerPre(i);
    if ((cache.cacheobjs[i].isEmpty == 0) && (strcmp(url, cache.cacheobjs[i].cache_url) == 0)) /* 캐시가 비어있지 않고 찾는 url과 같으면 break*/
      break;
    readerAfter(i);
  }
  /* 다 돌았는데 못 찾음 */
  if (i >= CACHE_OBJS_COUNT)
    return -1;
  /* 캐시 블록의 인덱스 반환 */
  return i;
}

/* find the empty cacheObj or which cacheObj should be evictioned */
int cache_eviction()
{
  int min = LRU_MAGIC_NUMBER;
  int minindex = 0;
  int i;
  for (i = 0; i < CACHE_OBJS_COUNT; i++) /* 10개의 캐시 블록을 전부 돌면서 */
  {
    readerPre(i);
    if (cache.cacheobjs[i].isEmpty == 1)
    { /* 캐시 블록이 비어있다면 */
      minindex = i;
      readerAfter(i);
      break;
    }
    if (cache.cacheobjs[i].LRU < min) /* LRU 값이 가장 작은 블록을 찾음 */
    {
      minindex = i;
      min = cache.cacheobjs[i].LRU;
      readerAfter(i);
      continue;
    }
    readerAfter(i);
  }

  return minindex;
}
/* 새로운 캐시 블록 제외하고 LRU 숫자 업데이트 */
void cache_LRU(int index)
{
  int i;
  for (i = 0; i < index; i++) /* 새로운 캐시 블록 전까지 */
  {
    writePre(i);
    if (cache.cacheobjs[i].isEmpty == 0) /* 캐시 블록이 비어있을 때 */
    {
      cache.cacheobjs[i].LRU--; /* LRU 숫자 줄이기 */
    }
    writeAfter(i);
  }
  i++;
  for (i; i < CACHE_OBJS_COUNT; i++) /* 새로운 캐시 블록 다음부터 캐시 블록 배열 끝까지 */
  {
    writePre(i);
    if (cache.cacheobjs[i].isEmpty == 0) /* 캐시 블록이 비어있을 때 */
    {
      cache.cacheobjs[i].LRU--; /* LRU 숫자 줄이기 */
    }
    writeAfter(i);
  }
}
/* uri와 내용을 캐시에 저장 */
void cache_uri(char *uri, char *buf)
{
  int i = cache_eviction(); /* 쓸 캐시 블록 */

  writePre(i);

  /* 캐시 블록에 내용 쓰기 */
  strcpy(cache.cacheobjs[i].cache_obj, buf);
  strcpy(cache.cacheobjs[i].cache_url, uri);
  cache.cacheobjs[i].isEmpty = 0;            /* 캐시 블록에 내용이 있다고 표시 */
  cache.cacheobjs[i].LRU = LRU_MAGIC_NUMBER; /* 가장 최근에 사용됐으니 표시 */
  cache_LRU(i);

  writeAfter(i); /*writer V*/
}