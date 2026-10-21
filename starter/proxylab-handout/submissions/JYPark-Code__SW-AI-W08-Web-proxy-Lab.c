/*
 * proxy.c - A simple concurrent HTTP proxy server with caching
 *
 * 흐름:
 *   [메인 스레드]
 *   1. 명령줄 포트 인자 검증
 *   2. SIGPIPE 무시 설정 (클라이언트 끊어져도 Proxy 생존)
 *   3. 캐시 초기화 (전역 cache_t, rwlock 포함)
 *   4. 리스닝 소켓 열기 (Open_listenfd)
 *   5. accept 루프:
 *      a. 힙에 connfd 저장 (race condition 방지)
 *      b. 새 스레드 생성 (Pthread_create)
 *      c. 메인은 바로 다음 accept (동시성 유지)
 *
 *   [워커 스레드]
 *   1. 전달받은 힙에서 connfd 로컬 복사
 *   2. Pthread_detach(Pthread_self())로 자원 자동 회수 예약
 *   3. Free(vargp)로 힙 해제
 *   4. doit(connfd)로 요청 처리
 *   5. Close(connfd)
 *
 *   [doit: 요청 처리 본체]
 *   1. 브라우저 요청 라인 읽기 (GET http://... HTTP/1.1)
 *   2. GET 메서드 검증
 *   3. parse_uri로 URL을 hostname/port/path 분해
 *   4. cache_find로 캐시 조회:
 *      - HIT: 캐시 데이터를 클라이언트로 전송하고 종료
 *      - MISS: 아래 5~9단계 진행
 *   5. build_requesthdrs로 HTTP/1.0 요청 재작성
 *      (Host 유지, User-Agent/Connection/Proxy-Connection 덮어쓰기)
 *   6. Open_clientfd로 백엔드 서버 접속
 *   7. 재작성된 요청 전송
 *   8. 응답 수신하며 클라이언트로 포워드
 *      (MAX_OBJECT_SIZE 이하면 동시에 로컬 버퍼에 누적)
 *   9. 누적 성공 시 cache_insert로 저장
 *  10. 백엔드 연결 종료
 *
 *   [캐시 — 전역 자원, Readers-Writers 동기화]
 *   - 이중 연결 리스트 기반 LRU (head=최근, tail=오래됨)
 *   - cache_find: rdlock (다중 reader 동시 허용)
 *   - cache_insert, cache_evict: wrlock (writer 단독)
 *   - MAX_CACHE_SIZE 초과 시 tail부터 evict
 *   - MAX_OBJECT_SIZE 초과 객체는 캐시 스킵 (포워딩만)
 */
#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* 캐시 엔트리: 이중 연결 리스트의 노드 */
typedef struct cache_entry {
    char uri[MAXLINE];              /* 캐시 키 (전체 URL) */
    char *data;                      /* 응답 데이터 (동적 할당) */
    int size;                        /* 데이터 크기 */
    struct cache_entry *prev;
    struct cache_entry *next;
} cache_entry_t;

/* 캐시 전체 */
typedef struct {
    cache_entry_t *head;             /* 가장 최근 사용 */
    cache_entry_t *tail;             /* 가장 오래 미사용 */
    int total_size;                  /* 현재 사용 중인 바이트 합 */
    pthread_rwlock_t lock;           /* Readers-Writers 락 */
} cache_t;

/* 전역 캐시 */
cache_t cache;

/* ------------------------------ 함수 프로토타입 ------------------------------ */
void doit(int clientfd);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void build_requesthdrs(rio_t *client_rio, char *newreq, 
                        char *hostname, char *port, char *path);
void *thread(void *vargp);
void cache_init(cache_t *c);                        /* 캐쉬 초기화 */
cache_entry_t *cache_find(cache_t *c, char *uri);   /* 캐쉬 조회 */
void cache_insert(cache_t *c, char *uri, 
                  char *data, int size);             /* 캐쉬에 저장 */
void cache_evict(cache_t *c);                        /* 캐쉬 : 가장 오래된 제거 */
/* ---------------------------------------------------------------------------- */

int main(int argc, char **argv)
{   
    /* uri 파싱 테스트 */
    // test_parse_uri();
    // exit(0);   

    int listenfd;
    // // 단일 쓰레드였을 때
    // int clientfd;
    pthread_t tid;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* 명령줄 인자 검증 */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* SIGPIPE 무시 (클라이언트 끊어져도 Proxy 생존) */
    Signal(SIGPIPE, SIG_IGN);
    /* 캐시 초기화 */
    cache_init(&cache);

    /* 리스닝 소켓 생성 (Tiny와 동일) */
    listenfd = Open_listenfd(argv[1]);

    /* 요청 받는 무한 루프 */
    while (1) {
        clientlen = sizeof(clientaddr);
        // 단일 쓰레드였을 때
        // clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        /* 힙에 connfd 저장 (race condition 방지) */
        int *connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        /* 접속한 클라이언트 정보 로그 */
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        
        // 단일 쓰레드였을 때
        // /* 요청 처리 (핵심!) */
        // doit(clientfd);
        // /* 연결 종료 */
        // Close(clientfd);


        /* 새 스레드로 요청 처리 */
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}




/*
 * doit - 한 HTTP 요청을 중계
 * 
 * 할 일:
 *   1. 브라우저 요청 라인 읽기 (GET http://... HTTP/1.1)
 *   2. parse_uri로 host/port/path 추출
 *   3. build_requesthdrs로 새 요청 조립
 *   4. Open_clientfd로 실제 서버에 접속
 *   5. 새 요청 전송
 *   6. 응답 받아서 브라우저로 전달
 */
void doit(int clientfd)
{
    /* TODO: doit 함수 채우기 */
    // printf("doit called\n");
    char buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char newreq[MAXLINE];          // 백엔드에 보낼 요청
    int serverfd;
    ssize_t n;
    rio_t client_rio, server_rio;


    /* 1. 브라우저 요청 라인 읽기 */
    Rio_readinitb(&client_rio, clientfd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))
        return;
    printf("Request line: %s", buf);

    /* 2. 요청 라인 파싱 */
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        printf("Not GET method, ignoring\n");
        return;
    }
    printf("URI: %s\n", uri);

    /* 3. URL 파싱 */
    if (parse_uri(uri, hostname, port, path) < 0) {
        printf("parse_uri failed\n");
        return;
    }

    /* Cache 처리 : 캐시 조회 (HIT면 바로 응답) */
    /* 3. 캐시 조회 (HIT면 바로 응답) */
    pthread_rwlock_rdlock(&cache.lock);
    cache_entry_t *cached = cache_find(&cache, uri);
    if (cached != NULL) {
        Rio_writen(clientfd, cached->data, cached->size);
        pthread_rwlock_unlock(&cache.lock);
        return;
    }
    pthread_rwlock_unlock(&cache.lock);

    /* 4. 새 요청 조립 */
    build_requesthdrs(&client_rio, newreq, hostname, port, path);

    /* 5. 백엔드 서버에 접속 */
    serverfd = Open_clientfd(hostname, port);
    Rio_readinitb(&server_rio, serverfd);

    /* 6. 새 요청 전송 */
    Rio_writen(serverfd, newreq, strlen(newreq));

    /* Cache 처리 : 응답 -> 클라이언트 전달 + 캐시 버퍼 누적 */
    char cache_buf[MAX_OBJECT_SIZE];
    int cache_size = 0;
    int too_big = 0;

    /* 7. 응답 받아서 브라우저로 그대로 전달 */
    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
        Rio_writen(clientfd, buf, n);

        /* 캐시 버퍼에 누적 (오버플로 체크) */
        if (!too_big) {
            if (cache_size + n <= MAX_OBJECT_SIZE) {
                memcpy(cache_buf + cache_size, buf, n);
                cache_size += n;
            } else {
                too_big = 1;  /* 너무 커서 캐시 포기 */
            }
        }

    }

    /* Cache 처리 : 캐시에 저장 (작으면) */
    if (!too_big && cache_size > 0) {
        pthread_rwlock_wrlock(&cache.lock);
        cache_insert(&cache, uri, cache_buf, cache_size);
        pthread_rwlock_unlock(&cache.lock);
    }

    /* 8. 백엔드 연결 종료 */
    Close(serverfd);
}

// uri 테스트 코드
void test_parse_uri() {
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char *test_cases[] = {
        "http://www.cmu.edu:8080/hub/index.html",
        "http://www.cmu.edu/hub/index.html",
        "http://localhost:15213/home.html",
        "http://example.com/",
        "http://example.com",
        "ftp://invalid.com/"
    };
    
    for (int i = 0; i < 6; i++) {
        int ret = parse_uri(test_cases[i], hostname, port, path);
        printf("URI: %s\n", test_cases[i]);
        if (ret == 0)
            printf("  host=%s, port=%s, path=%s\n", hostname, port, path);
        else
            printf("  FAIL (invalid URI)\n");
    }
}


/*
 * parse_uri - "http://host:port/path" 형태의 URL을 분해
 * 
 * 입력 예:
 *   uri = "http://www.cmu.edu:8080/index.html"
 * 출력:
 *   hostname = "www.cmu.edu"
 *   port     = "8080"
 *   path     = "/index.html"
 * 
 * port 생략 시: "http://example.com/page" → port = "80"
 * path 생략 시: "http://example.com"      → path = "/"
 * 
 * 성공: 0 반환, 실패: -1 반환
 */
int parse_uri(char *uri, char *hostname, char *port, char *path)
{
    /* TODO: 여기를 채워야 함 */
    char *hostbegin, *hostend, *pathbegin, *portpos;
    int len;

    /* 1. http:// 확인 */
    if (strncasecmp(uri, "http://", 7) != 0 ) {
        return -1;
    } 
    hostbegin = uri + 7; /* http:// 건너뛰기 */

    /* 2. path 시작점 (첫 '/' 찾기) */
    pathbegin = strchr(hostbegin, '/');

    if (pathbegin == NULL){
        /* path 없음 예: "http://example.com" */
        strcpy(path, "/");
        hostend = hostbegin + strlen(hostbegin);
    } else {
        /* path 있음 */
        strcpy(path, pathbegin); /* "/home.html" */
        hostend = pathbegin;
    }

    /* 3. host:port 부분에서 ":" 찾기 */
    portpos = NULL;
    for (char *p = hostbegin; p < hostend; p++) {
        if(*p == ':') {
            portpos = p;
            break;
        }
    }

    if (portpos != NULL) {
        /* ':' 있음 */
        len = portpos - hostbegin;
        strncpy(hostname, hostbegin, len);
        hostname[len] = '\0';

        len = hostend - portpos - 1;
        strncpy(port, portpos + 1, len);
        port[len] = '\0';
    } else {
        /* ':' 없음, 기본 포트 80 */
        len = hostend - hostbegin;
        strncpy(hostname, hostbegin, len);
        hostname[len] = '\0';
        strcpy(port, "80");
    }

    return 0;
}

/*
 * build_requesthdrs - 백엔드로 보낼 HTTP 요청 조립
 * 
 * 입력:
 *   client_rio: 브라우저 쪽 RIO 버퍼 (나머지 헤더 읽기용)
 *   hostname, port, path: parse_uri 결과
 * 
 * 출력 (newreq에 조립):
 *   "GET /path HTTP/1.0\r\n"
 *   "Host: hostname:port\r\n"
 *   "User-Agent: 고정문자열\r\n"
 *   "Connection: close\r\n"
 *   "Proxy-Connection: close\r\n"
 *   (브라우저가 보낸 기타 헤더들 그대로)
 *   "\r\n"
 */
void build_requesthdrs(rio_t *client_rio, char *newreq, 
                        char *hostname, char *port, char *path)
{
    /* TODO: */
    // newreq[0] = '\0';  // 빈 문자열로 초기화 (Rio_writen이 0바이트 쓰도록)

    char buf[MAXLINE];
    int has_host = 0;

    /* 1. 요청 라인 (GET /path HTTP 1.0) */
    sprintf(newreq, "GET %s HTTP/1.0\r\n", path);

    /* 2. 브라우저가 보낸 헤더들 한 줄 씩 읽으면서 처리 */
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        /* 빈 줄이면 헤더 끝 */
        if (strcmp(buf, "\r\n") == 0){
            break;
        }

        /* host 헤더 : 브라우저 것 그대로 전달 (발견 표시) */
        if (strncasecmp(buf, "Host:", 5) == 0) {
            strcat(newreq, buf);
            has_host =1;
            continue;
        }

        /* 이 헤더들은 버림 (우리가 아래서 직접 추가) */
        if (strncasecmp(buf, "User-Agent:", 11) == 0 ||
            strncasecmp(buf, "Connection:", 11) == 0 ||
            strncasecmp(buf, "Proxy-Connection:", 17) == 0) {
            continue;
        }


        /* 나머지 헤더들은 그대로 전달 */
        strcat(newreq, buf);
    }

    /* 3. Host 헤더가 없을 시, 채우기 */
    if (!has_host) {
        sprintf(buf, "Host: %s:%s\r\n", hostname, port);
        strcat(newreq, buf);
    }

    /* 4. 필수 헤더 덮어쓰기 */
    strcat(newreq, user_agent_hdr); /* 고정 User-Agent */
    strcat(newreq, "Connection: close\r\n");
    strcat(newreq, "Proxy-Connection: close\r\n");

    /* 5. 빈 줄로 헤더 종료 */
    strcat(newreq, "\r\n");

}

/*
 * thread - 각 클라이언트 요청을 처리하는 스레드 함수
 */
void *thread(void *vargp)
{
    int connfd = *((int *)vargp);         /* 로컬 복사 */
    Pthread_detach(Pthread_self());       /* 자동 정리 */
    Free(vargp);                          /* 힙 해제 */
    doit(connfd);                         /* 실제 처리 */
    Close(connfd);                        /* 연결 종료 */
    return NULL;
}

/*
 * 캐시 초기화
 */

void cache_init(cache_t *c)
{
    /* 빈 리스트로 초기화 */
    /* head = NULL, tail = NULL */
    /* total_size = 0 */

    c->head = NULL;
    c->tail = NULL;
    c->total_size = 0;
    
    /* rwlock 초기화 */
    pthread_rwlock_init(&c->lock, NULL);
}

/*
 * 캐시 제거
 */
void cache_evict(cache_t *c)
{
    /* 1. 비어있으면 return */
    if (c->tail == NULL) return;
    
    /* 2. 제거할 노드 = tail */
    cache_entry_t *victim = c->tail;
    
    /* 3. total_size 감소 */
    c->total_size -= victim->size;
    
    /* 4. tail 갱신 + 연결 끊기 */
    //   case A: victim만 있는 경우 (head == tail == victim)
    //       → c->head = NULL, c->tail = NULL
    //   case B: 여러 개 있는 경우
    //       → c->tail = victim->prev
    //       → c->tail->next = NULL
    if ( victim -> prev == NULL ) {
        /* case A : 노드가 하나만 있는 경우 */
        c-> head = NULL;
        c-> tail = NULL;
    } else {
        /* case B : 여러 개 있었음 */
        c->tail = victim->prev;
        c->tail->next = NULL;
    }

    /* 5. 메모리 해제 */
    Free(victim->data);
    Free(victim);
}

/*
 * 캐시 순회 + 일치하는 노드 찾는 로직
 */
cache_entry_t *cache_find(cache_t *c, char *uri)
{
    cache_entry_t *entry;
    
    /* 순회하며 uri 일치하는 노드 찾기 */
    for (entry = c->head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->uri, uri) == 0) {
            return entry;   /* 찾으면 그냥 반환 (LRU 이동 없음) */
        }
    }
    
    return NULL;   /* 못 찾음 */
}

/*
 * 캐시 생성  
 */
void cache_insert(cache_t *c, char *uri, char *data, int size)
{   
    /* 1. size > MAX_OBJECT_SIZE 캐시 안 함 */
    if (size > MAX_OBJECT_SIZE){
        return;
    }

    /* 2. 공간 확보 : total_size _ size > MAX_CACHE_SIZE면 evict 반복 */
    while (c->total_size + size > MAX_CACHE_SIZE) {
        cache_evict(c);
    }

    /* 3. 새 엔트리 생성 */
    cache_entry_t *entry = Malloc(sizeof(cache_entry_t));
    strcpy(entry->uri, uri);
    entry->data = Malloc(size);
    memcpy(entry->data, data, size);
    entry->size = size;

    /* 4. head에 삽입 (이중 연결 리스트) */
    entry->prev = NULL;
    entry->next = c->head;
    if (c->head != NULL){
        c->head->prev = entry;
    } else {
        /* 빈 리스트 일 경우 -> tail도 지정 */
        c -> tail = entry;
    }
    c->head = entry;

    /* 5. total size 갱신 */
    c->total_size += size;

}


