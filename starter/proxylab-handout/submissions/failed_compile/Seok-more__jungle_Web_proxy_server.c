#include <stdio.h>
#include "csapp.h"
#include <pthread.h>


// MAX_CACHE_SIZE = 1 MiB
// MAX_OBJECT_SIZE = 100 KiB
// where T is the maximum number of active connections: MAX_CACHE_SIZE + T * MAX_OBJECT_SIZE
// approximates a least-recently-used (LRU) eviction policy -> 앞뒤 연결 리스트 써야함
// using Pthreads readers-writers locks


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// 1. 포트로 바인딩해서 클라이언트로부터 HTTP 요청 받기
// 2. 클라이언트 요청을 받아서 실제 웹 서버(tiny)로 전달
// 3. 웹 서버의 응답을 받아서 다시 클라이언트(브라우저)에게 전달
// 4. + 동시 병렬 처리
// 5. + 캐싱 웹 오브젝트

// [클라이언트] ----HTTP요청----> [프록시] ----HTTP요청----> [tiny]
//            <----HTTP응답----         <----HTTP응답----    

// 캐시 구조체
typedef struct cache_block
{
  char uri[MAXLINE];        // 요청된 URI (key 역할)
  char *object;             // 실제 응답 데이터 (heap에 동적 할당)
  int size;                 // object 크기 (바이트)

  struct cache_block *prev;  // 이전 블록 (LRU 리스트)
  struct cache_block *next;  // 다음 블록 (LRU 리스트)
} cache_block_t;

typedef struct cache_list
{
    cache_block_t *head;       // LRU 기준: 가장 오래된 블록
    cache_block_t *tail;       // LRU 기준: 가장 최근 사용된 블록
    int total_size;            // 현재 캐시된 전체 바이트 수
 
    pthread_rwlock_t lock;     // 읽기/쓰기 보호용 락
} cache_list_t;


void doit(int fd);
void read_requesthdrs(rio_t *rp, char *header_host, char *header_other);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void reassemble(char *req, char *method, char *path, char *hostname, char *other_header);
void forward_response(int serverfd, int fd, int is_head);

//////////////////////////////
// 캐시관련
cache_list_t cache;
void cache_init();
void cache_free_block(cache_block_t *block);
void cache_clear(cache_list_t *cache);
void cahce_node_end(cache_list_t *cache, cache_block_t *node);
void cache_evict(cache_list_t *cache, int size_needed);
void cache_insert(cache_list_t *cache, const char *uri, const char *object, int size);
int cache_find(cache_list_t *cache, const char *uri, char *object_buf, int *size_buf);
//////////////////////////////


void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  free(vargp); // 동적 할당 해제
  doit(connfd);
  Close(connfd); // 클라이언트 소켓 해제
  return NULL;
}

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  cache_init();

  // 명령 인자 체크 -> 포트 번호 있냐 없냐
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 서버 리스닝 소켓 생성 (포트번호로)
  listenfd = Open_listenfd(argv[1]);
  printf("Proxy listening on port %s\n", argv[1]); 
  fflush(stdout);
  
  while (1)
  {
    // 클라이언트 연결을 기다림
    clientlen = sizeof(clientaddr);
    int *connfdp = malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); // line:netp:tiny:accept
    
    // 클라이언트의 호스트명과 포트 번호 얻기
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // // 실제 요청 처리 함수 호출
    // doit(connfd);  // 클라이언트 요청 처리
    // // 연결 종료
    // Close(connfd); // 클라이언트와의 연결 닫기

    // 병렬 처리
    pthread_t tid;
    pthread_create(&tid, NULL, thread, connfdp);
    pthread_detach(tid);
  }
}

// 클라이언트의 HTTP 요청을 읽고
// 타겟 서버(tiny)에 맞게 요청을 변환해서 전달
// 타겟 서버의 응답을 받아
// 다시 클라이언트에게 전달하는 함수
void doit(int fd)
{
  int is_head = 0;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char header_host[MAXLINE], header_other[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  char request[MAXLINE];
  rio_t rio;

  // 클라이언트와 연결된 소켓을 robust I/O 구조체에 바인딩
  Rio_readinitb(&rio, fd);

  // 요청 라인(첫 번째 줄)을 읽어서 buf에 저장 (예: "GET /index.html HTTP/1.0")
  if (!Rio_readlineb(&rio, buf, MAXLINE)) return;

  // 요청 라인 출력 (디버깅용)
  printf("Request line: %s", buf);

  // GET /index.html HTTP/1.1\r\n
  // /index.html
  // GET http://localhost:12345/index.html HTTP/1.1\r\n (프록시 요청)
  // HEAD /image.png HTTP/1.0\r\n
  // 요청 라인을 method, uri, version으로 분리해서 저장
  sscanf(buf, "%s %s %s", method, uri, version);

  // 지원하지 않는 HTTP 메서드일 경우 에러 메시지 전송 후 함수 종료
  if (strcasecmp(method, "HEAD") == 0) 
  {
    strcpy(method, "GET"); 
    is_head = 1;
  } 
  else if (strcasecmp(method, "GET")) 
  {
    clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  // 추가적인 요청 헤더(Host, User-Agent 등) 읽어서 무시 (기능 확장 가능)
  read_requesthdrs(&rio, header_host, header_other);

  // URI를 파싱해서 hostname(웹서버 주소), port(웹서버 포트), path(요청 경로) 추출
  parse_uri(uri, hostname, port, path);

  // 캐시 체크
  char object_buf[MAX_OBJECT_SIZE];
  int object_size = 0;
  if (cache_find(&cache, uri, object_buf, &object_size)) 
  {
    // 캐시에 있으면 바로 클라이언트에게 전송
    if (!is_head)
    {
      Rio_writen(fd, object_buf, object_size);
    }
    return;
  }

  // 캐시가 없으면 서버에 요청, 추출한 hostname과 port로 웹서버(tiny)에 연결해서 서버 소켓 생성
  int serverfd = Open_clientfd(hostname, port);
  if (serverfd < 0) 
  {
    clienterror(fd, hostname, "502", "Bad Gateway", "Proxy couldn't connect to server");
    return;
  }

  // 클라이언트의 요청을 웹서버에 맞는 형식(HTTP/1.0 등)으로 재조립
  // request에는 최종적으로 tiny 서버에 보낼 요청이 들어감
  reassemble(request, method,path, hostname, header_other);

  // 재조립된 request를 웹서버(서버 소켓)에 전송
  Rio_writen(serverfd, request, strlen(request));

  // 서버에서 데이터 받아서 클라이언트로 전달 + 캐싱
  rio_t rio_server;                    // 서버(tiny) 소켓에 robust I/O 구조체 바인딩.
  char cache_object[MAX_OBJECT_SIZE];  // 캐시에 저장할 데이터 버퍼 (최대 100 KiB).
  int cache_size = 0;                  // 현재까지 받은 데이터의 총 크기.
  ssize_t n;                           // 한 번에 읽어온 데이터의 바이트 수.

  Rio_readinitb(&rio_server, serverfd);

  // 서버(tiny)로부터 데이터를 반복해서 읽음, 읽은 데이터가 있으면 반복
  while ((n = Rio_readnb(&rio_server, buf, MAX_OBJECT_SIZE)) > 0) 
  {
    // 클라이언트에 전달
    if (!is_head)
    {
      Rio_writen(fd, buf, n);
    }

    // 캐시할 오브젝트 크기 제한 이내라면 캐시 버퍼에 쌓기
    if (cache_size + n <= MAX_OBJECT_SIZE)
    {
      memcpy(cache_object + cache_size, buf, n);
    }
    cache_size += n;
  }

  // 웹서버의 응답을 받아서 클라이언트에게 그대로 전달 -> 캐시 생겨서 패스
  // forward_response(serverfd, fd, is_head);

  // 타겟 서버와의 연결 닫기 (자원 해제)
  Close(serverfd);

  // 다 읽고 캐싱
  if (cache_size > 0 && cache_size <= MAX_OBJECT_SIZE)
  {
    cache_insert(&cache, uri, cache_object, cache_size);
  }
}

// HTTP 요청의 헤더 부분을 한 줄씩 읽어서 Host 헤더와 기타 헤더를 분리하는 함수
void read_requesthdrs(rio_t *rp, char *header_host, char *header_other)
{
  char buf[MAXLINE];

  // 출력 버퍼를 빈 문자열로 초기화
  header_host[0] = '\0';
  header_other[0] = '\0';

  // 헤더의 끝은 "\r\n" (빈 줄)이므로, 빈 줄을 만날 때까지 반복
  while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n")) 
  {
    // Host 헤더는 별도로 저장 (여러 개일 때 첫 번째만 저장)
    if (!strncasecmp(buf, "Host:", 5)) 
    {
      if (header_host[0] == '\0')
      {
        strncpy(header_host, buf, MAXLINE - 1);
      }
        
    }
    // 프록시가 직접 생성/수정할 헤더는 무시
    else if (!strncasecmp(buf, "User-Agent:", 11) ||
             !strncasecmp(buf, "Connection:", 11) ||
             !strncasecmp(buf, "Proxy-Connection:", 17)) 
    {
      continue;
    }

    // 나머지 헤더는 header_other에 누적 (오버플로우 방지)
    else 
    {
      // 남은 공간 계산
      int remain = MAXLINE - strlen(header_other) - 1;
      if (remain > 0)
      {
        strncat(header_other, buf, remain);
      }
    }
  }
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *hostbegin, *hostend, *portbegin, *pathbegin;
  char buf[MAXLINE];

  // uri를 buf에 복사 (원본 파괴 방지)
  strcpy(buf, uri);

  // "//" 위치를 찾아 hostbegin 지정 (http:// 생략 가능)
  hostbegin = strstr(buf, "//");
  hostbegin = (hostbegin != NULL) ? hostbegin + 2 : buf; 

  // 경로(path) 시작 위치 찾기. 없으면 기본 "/"
  pathbegin = strchr(hostbegin, '/');
  if (pathbegin != NULL)
  {
    strcpy(path, pathbegin);      // path에 경로 복사
    *pathbegin = '\0';            // host 부분 문자열 끝 지정
  }
  else
  {
    strcpy(path, "/");
  }

  // 포트 번호(:포트) 위치 찾기
  portbegin = strchr(hostbegin, ':');
  if (portbegin != NULL) 
  {
    *portbegin = '\0';            // host 부분 문자열 끝 지정
    strcpy(hostname, hostbegin);  // hostname 복사
    strcpy(port, portbegin + 1);  // 포트번호 복사
  } 
  else 
  {
    strcpy(hostname, hostbegin);  // 포트 없으면 hostname 복사
    strcpy(port, "80");           // 기본 포트 80
  }
}

// 프록시가 tiny 서버에 전달할 HTTP 요청을 재조립하는 함수 (GET/HEAD 등 메서드 지원)
void reassemble(char *req, char *method, char *path, char *hostname, char *other_header)
{
  // method 인자를 사용하여 요청 라인을 동적으로 생성
  sprintf(req,
    "%s %s HTTP/1.0\r\n"                // 요청 라인 (GET/HEAD 등 메서드)
    "Host: %s\r\n"                      // Host 헤더
    "%s"                                // User-Agent 헤더 (전역 상수)
    "Connection: close\r\n"             // 연결 종료 명시
    "Proxy-Connection: close\r\n"       // 프록시 연결 종료 명시
    "%s"                                // 기타 헤더
    "\r\n",                             // 헤더 끝 표시
    method,
    path,
    hostname,
    user_agent_hdr,
    other_header
  );
}

// 서버(tiny 등)의 응답을 받아 클라이언트에게 그대로 전달하는 함수
void forward_response(int serverfd, int fd, int is_head)
{
    rio_t rio_server;                   // 서버 소켓에 대한 robust I/O 구조체
    char buf[MAX_OBJECT_SIZE];          // 서버로부터 읽어올 데이터 버퍼 (최대 캐시 오브젝트 크기)
    ssize_t n;                          // 읽어온 데이터의 바이트 수

    Rio_readinitb(&rio_server, serverfd); // 서버 소켓을 robust I/O 구조체에 바인딩

    // 서버로부터 데이터를 반복적으로 읽어서 클라이언트로 전달
    while ((n = Rio_readnb(&rio_server, buf, MAX_OBJECT_SIZE)) > 0) 
    {
        if (!is_head) 
        {                               // HEAD 요청이 아니라면
            Rio_writen(fd, buf, n);     // 클라이언트 소켓으로 읽은 데이터 전송
        }
        // HEAD 요청이면 읽기만 하고 클라이언트로 보내지 않음
    }
}

// 클라이언트(웹브라우저)가 잘못된 요청을 보냈을 때,
// HTTP 에러 응답(예: 404 Not Found, 501 Not Implemented 등)을
// HTML 형식으로 만들어서 클라이언트에게 보내주는 함수
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // 에러 응답의 HTML 본문(body) 생성
  sprintf(body,"<html><title>Tiny Error</title>");
  sprintf(body,"%s<body bgcolor=\"ffffff\">\r\n",body); // 배경색 흰색
  sprintf(body,"%s%s: %s\r\n",body, errnum, shortmsg); // 에러 코드 및 짧은 설명
  sprintf(body,"%s<p>%s: %s\r\n",body, longmsg, cause); // 긴 설명 및 원인
  sprintf(body,"%s<hr><em>The Proxy Web server</em>\r\n",body); // 서버 정보

  // HTTP 상태줄 생성 및 전송
  sprintf(buf,"HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));

  // Content-Type 헤더 전송 (HTML)
  sprintf(buf,"Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // Content-Length 헤더 전송 (본문 길이)
  sprintf(buf,"Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));

  // HTML 본문(body) 전송
  Rio_writen(fd, body, strlen(body));
}

///////////////////////////////////////////////////////////////////////////
// 캐시 관련

// 캐시 초기화
void cache_init()
{
  cache.head = NULL; // LRU 리스트의 head(가장 오래된) 포인터 초기화
  cache.tail = NULL; // LRU 리스트의 tail(가장 최근) 포인터 초기화
  cache.total_size = 0; // 현재 캐시된 전체 바이트 수 초기화
  pthread_rwlock_init(&cache.lock, NULL); // 읽기/쓰기 락 초기화 (동시성 제어)
}

// 캐시 블럭 정리
void cache_free_block(cache_block_t *block)
{
  if (block) 
  {
    free(block->object);
    free(block);
  }
}

// 캐시 정리 
void cache_clear(cache_list_t *cache)
{
  pthread_rwlock_wrlock(&cache->lock); // 캐시 수정이므로 쓰기 락

  cache_block_t *node = cache->head;
  while (node) 
  {
    cache_block_t *next = node->next;
    cache_free_block(node);
    node = next;
  }

  cache->head = NULL;
  cache->tail = NULL;
  cache->total_size = 0;

  pthread_rwlock_unlock(&cache->lock);

}


// LRU 리스트에서 node를 가장 뒤(최근 사용)로 이동시킴
void cahce_node_end(cache_list_t *cache, cache_block_t *node) 
{
    if (node == cache->tail) return; 
 
    // 리스트에서 node 제거
    if (node->prev)
    {
      node->prev->next = node->next; 
    } 
    else // node가 head
    {
      cache->head = node->next; 
    }
 
    if (node->next) 
    {
      node->next->prev = node->prev; 
    }
    else // node가 tail
    { 
      cache->tail = node->prev; 
    }
 
    // 끝에 붙임
    node->prev = cache->tail;
    node->next = NULL;
 
    if (cache->tail)
    {
      cache->tail->next = node; 
    }    
    else // 캐시가 비었음
    {
      cache->head = node;
    }
    
    // tail 갱신
    cache->tail = node; 
}

// 캐시가 넘칠 경우, 오래된 것부터 삭제(LRU)하는 함수
void cache_evict(cache_list_t *cache, int size_needed)
{
  // 캐시에 새 데이터(size_needed)를 넣었을 때 용량 초과면 반복적으로 evict
  while (cache->total_size + size_needed > MAX_CACHE_SIZE)
  {
    if (cache->head == NULL) return; // 캐시가 비었으면 종료
 
    cache_block_t *oldest = cache->head; // 가장 오래된 블록 선택
 
    cache->head = oldest->next; // head를 다음 블록으로 이동
    if (cache->head)
    {
      cache->head->prev = NULL; // head의 prev를 NULL로
    }
    else
    {
      cache->tail = NULL; // 리스트가 비었을 경우 tail도 NULL
    }
 
    cache->total_size -= oldest->size; // 캐시 사용량 감소
    
    cache_free_block(oldest);
  }
}

// 새로운 오브젝트를 캐시에 삽입하는 함수
void cache_insert(cache_list_t *cache, const char *uri, const char *object, int size)
{
  if (size > MAX_OBJECT_SIZE) return; // 오브젝트가 너무 크면 캐싱하지 않음
 
  pthread_rwlock_wrlock(&cache->lock); // 쓰기 락 획득
 
  cache_evict(cache, size); // 삽입 전 용량 초과된 부분 삭제
 
  cache_block_t *new_block = malloc(sizeof(cache_block_t)); // 새 블록 생성
  if (!new_block)
  {
    pthread_rwlock_unlock(&cache->lock);
    return;
  }
 
  strncpy(new_block->uri, uri, MAXLINE - 1); // URI 복사 (key 역할)
  new_block->uri[MAXLINE - 1] = '\0';
 
  new_block->object = malloc(size); // 오브젝트 데이터 저장을 위한 메모리 할당
  if (!new_block->object)
  {
    cache_free_block(new_block);
    pthread_rwlock_unlock(&cache->lock);
    return;
  }
 
  memcpy(new_block->object, object, size); // 실제 응답 데이터 복사
  new_block->size = size;
 
  new_block->prev = cache->tail; // 새 블록을 tail 뒤에 연결
  new_block->next = NULL;
 
  if (cache->tail)
  {
    cache->tail->next = new_block;
  } 
  else
  {
    cache->head = new_block; // 캐시가 비었으면 head도 새 블록
  }
 
  cache->tail = new_block; // tail을 새 블록으로 설정
  cache->total_size += size; // 캐시 사용량 증가
 
  pthread_rwlock_unlock(&cache->lock); // 락 해제
}

// 캐시에서 해당 URI에 맞는 오브젝트를 찾아 object_buf에 복사해주는 함수
// 반환값: 1(성공) / 0(실패)
int cache_find(cache_list_t *cache, const char *uri, char *object_buf, int *size_buf)
{
  pthread_rwlock_wrlock(&cache->lock); // 처음부터 쓰기 락

  cache_block_t *node = cache->head;
  while (node)
  {
    if(strcmp(node->uri, uri) == 0)
    {
      cahce_node_end(cache, node); // 최근 사용으로 tail로 이동
      memcpy(object_buf, node->object, node->size); // 데이터 복사
      *size_buf = node->size;

      pthread_rwlock_unlock(&cache->lock); // 락 해제
      return 1;
    }
    node = node->next;
  }

  pthread_rwlock_unlock(&cache->lock); // 못 찾은 경우 락 해제
  return 0;
}