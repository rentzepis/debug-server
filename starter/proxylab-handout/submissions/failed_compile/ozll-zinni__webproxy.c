#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000 // 캐시 및 객체의 최대 크기
#define MAX_OBJECT_SIZE 102400 // 객체의 최대 크기
#define LRU_MAGIC_NUMBER 9999  // LRU 알고리즘에 사용되는 매직 넘버
#define CACHE_OBJS_COUNT 10    // 캐시 객체의 수

/* User-Agent 헤더 */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n"; // 브라우저 정보 헤더

/* doit 함수 선언 */
void p_doit(int fd);

/* request 헤더 읽기 함수 선언 */
void p_read_requesthdrs(rio_t *rp, char *hostname, char *port, char *request_header);

/* URI 파싱 함수 선언 */
void p_parse_uri(char *uri, char *hostname, char *path, char *port);

/* 쓰레드 함수 선언 */
void *thread(void *vargp);

/* 캐시 초기화 함수 */
void cache_init();


/* URI를 캐시에 저장하는 함수 */
void cache_uri(char *uri, char *buf);


/* 캐시 블록 구조체 정의 */
typedef struct
{
  char cache_obj[MAX_OBJECT_SIZE]; // 캐시된 객체
  char cache_url[MAXLINE];         // 캐시된 URL
  int LRU;                         // LRU 우선순위
  int isEmpty;                     // 캐시 블록이 비어 있는지 여부

  int readCnt;      // 리더 카운트
  sem_t wmutex;     // 캐시 접근 보호 뮤텍스
  sem_t rdcntmutex; // readCnt 접근 보호 뮤텍스
} cache_block;

/* 캐시 구조체 정의 */
typedef struct
{
  cache_block cacheobjs[CACHE_OBJS_COUNT]; // 캐시 블록 배열
  int cache_num;                           // 캐시 번호 (사용되지 않음)
} Cache;

/* 전역 캐시 변수 선언 */
Cache cache;

int main(int argc, char **argv)
{
  int listenfd, *clientfd;               // 소켓 및 클라이언트 파일 디스크립터 변수 선언
  char hostname[MAXLINE], port[MAXLINE]; // 호스트명 및 포트 문자열 배열 선언
  socklen_t clientlen;                   // 클라이언트 주소 길이 변수
  struct sockaddr_storage clientaddr;    // 클라이언트 주소 구조체
  pthread_t tid;                         // 스레드 ID 변수 선언

  /* 명령행 인자 확인 */
  if (argc != 2) // 명령행 인자 개수 확인
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]); // 사용법 출력
    exit(1);                                        // 프로그램 종료
  }

  /* SIGPIPE 시그널 무시 */
  Signal(SIGPIPE, SIG_IGN); // SIGPIPE 시그널 무시 설정

  /* 포트로 대기 소켓 생성 */
  listenfd = Open_listenfd(argv[1]); // 지정된 포트로 소켓 열기

  /* 클라이언트 연결 대기 */
  while (1) // 무한 루프
  {
    clientlen = sizeof(clientaddr);                                                 // 클라이언트 주소 길이 설정
    clientfd = (int *)Malloc(sizeof(int));                                          // 클라이언트 파일 디스크립터 동적 할당
    *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                    // 클라이언트 연결 수락
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트 호스트명 및 포트 추출
    printf("Accepted connection from (%s, %s)\n", hostname, port);                  // 연결 수락 메시지 출력
    Pthread_create(&tid, NULL, thread, clientfd);                                   // 새 스레드 생성 및 실행
  }
}

/* 쓰레드 함수 */
void *thread(void *vargp)
{
  int clientfd = *((int *)vargp);
  Pthread_detach((pthread_self()));
  Free(vargp);
  proxy(clientfd);
  Close(clientfd);
  return NULL;
}

/* 클라이언트 요청 처리 함수 */
void proxy(int clientfd)
{
  // 서버 소켓 파일 디스크립터를 선언합니다.
  int serverfd;
  // 버퍼와 문자열 변수를 선언합니다.
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], request_header[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  char ip[MAXLINE], port[MAXLINE], hostname[MAXLINE], path[MAXLINE], header[MAXLINE];
  // rio_t 구조체 변수를 선언합니다.
  rio_t rio, server_rio;
  // 변수를 초기화합니다.
  int len;

  /* 요청 라인 및 헤더 읽기 */
  // 클라이언트 소켓을 읽기 위해 초기화합니다.
  Rio_readinitb(&rio, clientfd);
  // 요청 헤더를 읽고 저장합니다.
  Rio_readlineb(&rio, request_header, MAXLINE);
  // 요청 헤더를 출력합니다.
  printf("1. Request headers:\n");
  printf("%s", request_header);
  // 요청 메서드와 URI를 추출합니다.
  sscanf(request_header, "%s %s", method, uri);

  // 캐시에 저장될 URI를 복사합니다.
  char url_store[MAXLINE];
  strcpy(url_store, uri);

  // URI를 파싱하여 호스트명, 경로, 포트를 추출합니다.
  p_parse_uri(uri, hostname, path, port);

  // 요청 헤더를 구성합니다.
  sprintf(request_header, "%s /%s %s\r\n", method, path, "HTTP/1.0");
  sprintf(request_header, "%sConnection: close\r\n", request_header);
  sprintf(request_header, "%sProxy-Connection: close\r\n", request_header);
  sprintf(request_header, "%s%s\r\n", request_header, user_agent_hdr);

  printf("\n%s %s %s\n", hostname, port, path);
  printf("%s", request_header);

  // 서버에 연결합니다.
  serverfd = Open_clientfd(hostname, port);
  // 서버 소켓을 읽기 위해 초기화합니다.
  Rio_readinitb(&server_rio, serverfd);
  // 요청 헤더를 서버에 전송합니다.
  Rio_writen(serverfd, request_header, strlen(request_header));

  // 캐시에 저장될 버퍼와 객체 크기를 초기화합니다.
  char cachebuf[MAX_OBJECT_SIZE];
  int sizebuf = 0;
  size_t n;

  /* 서버 응답 읽고 클라이언트에 전송 */
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
  {
    sizebuf += n;
    // 캐시 버퍼에 서버 응답을 복사합니다.
    if (sizebuf < MAX_OBJECT_SIZE)
      strcat(cachebuf, buf);
    // 클라이언트에 서버 응답을 전송합니다.
    Rio_writen(clientfd, buf, n);
  }

  // 서버 소켓을 닫습니다.
  Close(serverfd);
}

/* URI 파싱 함수 */
void p_parse_uri(char *uri, char *hostname, char *path, char *port)
{
  // URI에서 "//"을 찾아 포인터를 설정합니다.
  char *ptr = strstr(uri, "//");
  // 만약 "//"을 찾았다면 ptr을 이동시킵니다.
  ptr = ptr != NULL ? ptr + 2 : uri;

  // 호스트의 끝을 찾습니다.
  char *host_end = strchr(ptr, ':');
  // 만약 ":"이 없다면 경로의 끝을 찾습니다.
  if (host_end == NULL)
  {
    host_end = strchr(ptr, '/');
  }

  // 호스트명의 길이를 계산하고 호스트명을 복사합니다.
  int len = host_end - ptr;
  strncpy(hostname, ptr, len);
  hostname[len] = '\0';

  // 만약 ":"이 있다면 포트를 추출합니다.
  if (*host_end == ':')
  {
    char *port_start = host_end + 1;
    char *path_start = strchr(port_start, '/');
    // 만약 "/"이 없다면 잘못된 URI입니다.
    if (path_start == NULL)
    {
      printf("Invalid URI\n");
      exit(1);
    }
    // 포트의 길이를 계산하고 포트를 복사합니다.
    len = path_start - port_start;
    strncpy(port, port_start, len);
    port[len] = '\0';
  }

  // 경로의 시작을 찾습니다.
  char *path_start = strchr(ptr, '/');
  // 만약 경로가 존재한다면 경로를 복사합니다.
  if (path_start != NULL)
  {
    strcpy(path, path_start);
  }
  else
  {
    // 경로가 없다면 "/"를 복사합니다.
    strcpy(path, "/");
  }
}