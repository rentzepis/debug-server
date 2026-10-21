#include <stdio.h>

#include "cache.c" // 캐시 관련 함수 및 자료구조가 정의된 파일
#include "csapp.h" // CS:APP Wrapper 함수 포함

sem_t sem; // 캐시 접근 시 동기화를 위한 세마포어

/* function declaration (함수 선언) */
void doit(int); // 단일 연결(트랜잭션)을 처리하는 루틴이다.
void *thread(void *); // 스레드에서 실행될 함수이다.
void handle_client_headers(rio_t *, int, char *, char *); // 클라이언트 헤더를 처리하고 서버로 전달하는 함수이다.
void get_numeric_addr(char *, char *, u_int32_t *, u_int16_t *); // 호스트 이름과 서비스를 숫자 IP와 포트로 변환하는 함수이다.
char *strcasestr(const char *, const char *); // 대소문자를 무시하고 부분 문자열을 찾는 함수이다.
int parse_uri(char *, char *, char *, char *); // URI에서 호스트, 포트, 경로를 파싱하는 함수이다.
int handle_response(rio_t *, int, char *, ssize_t *); // 서버 응답을 처리하고 클라이언트에게 전달하며 캐시하는 함수이다.

/* global variable for user-agent header (User-Agent 헤더를 위한 전역 변수) */
static const char *user_agent_hdr =
    "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3";

/*==============================*/
// 메인 함수
int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    struct sockaddr_storage client;
    char host[MAXLINE], port[MAXLINE];
    socklen_t clientlen = sizeof(client);
    pthread_t tid;

    if (argc != 2)
    { /* port is argv[1] (포트 번호는 argv[1]이다) */
        printf("usage ./proxy <port>\n");
        exit(1);
    }

    /* semaphor (세마포어 초기화) */
    Sem_init(&sem, NULL, 1); // 세마포어를 1로 초기화하여 뮤텍스로 사용한다.

    /* init cache list (캐시 리스트 초기화) */
    // 이중 연결 리스트의 헤드와 테일 노드를 할당하고 연결한다.
    head = (cache_t *)malloc(sizeof(cache_t));
    tail = (cache_t *)malloc(sizeof(cache_t));
    head->next = tail;
    tail->prev = head;

    // 남은 캐시 공간을 저장할 전역 변수를 초기화한다.
    cache_left = (ssize_t *)malloc(sizeof(ssize_t));
    *cache_left = MAX_CACHE_SIZE;

    // 명령줄 인자로 받은 포트로 듣기 소켓을 연다.
    listenfd = Open_listenfd(argv[1]);
    
    // 무한 루프를 돌며 클라이언트의 연결을 수락한다.
    while (1)
    {
        // connfd를 스레드에 안전하게 전달하기 위해 힙에 할당한다.
        connfdp = (int *)malloc(sizeof(int));
        // 클라이언트 연결을 수락한다.
        *connfdp = Accept(listenfd, (SA *)&client, &clientlen);
        
        // 새로운 연결에 대해 스레드를 생성하고, thread 함수에 connfdp를 인자로 전달한다.
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0; // 이 코드는 무한 루프이므로 실제로 도달하지 않는다.
}

/** Handle a thread (스레드 처리)
 * @param connfdp         connfd의 포인터이다.
 */
void *thread(void *connfdp)
{
    int connfd = *((int *)connfdp);
    Pthread_detach(pthread_self()); // 스레드를 분리하여 종료 시 자원을 자동으로 회수하도록 한다.
    free(connfdp); // 힙에 할당했던 connfdp를 해제한다.
    doit(connfd); // 실제 연결 처리를 수행한다.
    Close(connfd); // 연결 소켓을 닫는다.
    return NULL;
}

/** Routine of a single connection (단일 연결 처리 루틴)
 * @param connfd         클라이언트와의 연결 소켓 파일 디스크립터이다.
 * @return               void
 */
void doit(int connfd)
{
    rio_t rio;          /* read & write를 위한 RIO 구조체이다. */
    int proxyfd;        /* 최종 서버와의 연결 소켓 파일 디스크립터이다. */
    char buf[MAXBUF];   /* 임시 버퍼이다. */

    char method[MAXLINE], uri[MAXLINE], version[MAXLINE]; /* 요청 라인 정보이다. */
    char host[MAXLINE], serv[MAXLINE], path[MAXLINE];     /* 호스트, 서비스(포트), 경로 정보이다. */

    u_int32_t ip;         /* 숫자 IP 주소(32비트)이다. */
    u_int16_t port;       /* 숫자 포트 번호(16비트)이다. */
    method_t method_code; /* 열거형 타입의 메서드 코드이다. */

    /*
     * Host & Serv  CAN  be string name (호스트와 서비스는 문자열 이름일 수 있다.)
     * Ip   & Port  MUST be numeric for cache (IP와 포트는 캐시를 위해 숫자여야 한다.)
     *
     * example)
     * host    : www.google.com
     * serv    : http
     * ip      : 111.222.333.444   -> 32bit uint32_t
     * port    : 80                -> 16bit uint16_t
     */

    /* get request (요청 받기) */
    Rio_readinitb(&rio, connfd); // 클라이언트 소켓에 RIO 구조체를 초기화한다.
    Rio_readlineb(&rio, buf, MAXBUF); // 요청 라인을 읽는다.
    sscanf(buf, "%s %s %s", method, uri, version); // 요청 라인에서 메서드, URI, 버전을 파싱한다.

    /* use HTTP/1.0 (HTTP/1.0 사용) */
    // 프록시가 서버로 보낼 요청의 버전을 HTTP/1.0으로 강제한다.
    strcpy(version, "HTTP/1.0");

    /* get hostname, service, path from uri (URI에서 호스트 이름, 서비스, 경로 추출) */
    if (parse_uri(uri, host, serv, path) > 0)
    {
        printf("* Not a valid request %s %s %s\n", host, serv, path);
        return; // 유효하지 않은 요청이면 함수를 종료한다.
    };

    /* log :) (로그 출력) */
    printf("\n* Target: %s %s\n", host, serv);
    printf("* Request: %s %s %s\n", method, path, version);

    /* get numeric ip & port, method_code (숫자 IP, 포트, 메서드 코드 얻기) */
    get_numeric_addr(host, serv, &ip, &port); // DNS 조회를 통해 숫자 IP를 얻는다.
    
    // 메서드 코드를 설정한다.
    if (!strcasecmp(method, "GET"))
        method_code = GET;
    else
        method_code = HEAD;

    /* check if response is cached (응답이 캐시되었는지 확인) */
    cache_t *meta = NULL;

    P(&sem); // 세마포어 P 연산 (캐시 읽기 락 획득)
    meta = read_cache(ip, port, method_code, path); // 캐시에서 데이터를 찾는다.
    V(&sem); // 세마포어 V 연산 (캐시 읽기 락 반납)
    
    /* if cached data exist (캐시된 데이터가 존재하면) */
    if (meta != NULL)
    {
        printf("* Use Cached Data: %d (%d byte)\n", meta->ip, meta->reslen);
        printf("%s", meta->response); // 캐시 응답의 일부를 출력한다.

        // 캐시된 응답을 클라이언트에게 바로 전송한다.
        Rio_writen(connfd, meta->response, meta->reslen);
        
        P(&sem);
        push_cache(meta); /* cache is read, push into top list (캐시를 읽었으므로 LRU를 위해 최상단 리스트로 이동시킨다.) */
        V(&sem);
        return;
    }

    /* open new socket for proxy - server (프록시-서버 연결을 위한 새로운 소켓 열기) */
    proxyfd = Open_clientfd(host, serv); // 최종 서버에 연결한다.
    printf("* No cached data. Visit server %d\n", proxyfd);

    /* send request to server (서버에게 요청 전송) */
    // 요청 라인을 구성하여 서버로 보낸다.
    sprintf(buf, "%s %s %s\r\n", method, path, version);
    Rio_writen(proxyfd, buf, strlen(buf));

    /* send all headers left to server (남은 모든 헤더를 서버로 전송) */
    handle_client_headers(&rio, proxyfd, host, serv); // 클라이언트의 나머지 헤더를 처리하고 서버로 전달한다.

    /* read response from server and send to client (서버로부터 응답을 읽어 클라이언트에게 전송) */
    char *cache_data;
    ssize_t *cache_len;
    // 응답 데이터를 캐시하기 위해 MAX_OBJECT_SIZE 크기의 버퍼를 할당한다.
    cache_data = (char *)malloc(MAX_OBJECT_SIZE);
    cache_len = (ssize_t *)malloc(sizeof(ssize_t));

    Rio_readinitb(&rio, proxyfd); // 서버 소켓에 RIO 구조체를 초기화한다.
    int rc = handle_response(&rio, connfd, cache_data, cache_len); // 응답 처리 및 클라이언트 전송
    
    // rc가 0이면 캐시 가능한 크기이다.
    if (!rc)
    { /* cache the response (응답 캐시) */
        P(&sem); // 세마포어 P 연산 (캐시 쓰기 락 획득)
        write_cache(ip, port, method_code, path, cache_data, cache_len); // 캐시에 응답을 기록한다.
        V(&sem); // 세마포어 V 연산 (캐시 쓰기 락 반납)
    }
}

/** Read response from server and send it to client (서버에서 응답을 읽어 클라이언트에게 전송)
 * @param rp              서버 소켓에 연결된 RIO 구조체 포인터이다.
 * @param fd              클라이언트 소켓 파일 디스크립터이다.
 * @param cache_data      응답을 저장할 버퍼 포인터이다.
 * @param cache_len       전체 응답 크기를 저장할 포인터이다.
 * @return                응답 크기가 캐시 제한을 초과하면 1, 아니면 0이다.
 */
int handle_response(rio_t *rp, int fd, char *cache_data, ssize_t *cache_len)
{
    char *p;                /* 문자열 분할을 위한 포인터이다. */
    char *cursor;           /* cache_data에 쓰기 위한 커서이다. */
    char buf[MAXBUF];       /* 읽기/쓰기를 위한 임시 버퍼이다. */
    int is_cacheable_res;   /* 응답 길이가 캐시 제한을 초과하는지 여부이다. (1: 초과, 0: 가능) */
    ssize_t content_len;    /* 본문의 길이이다. */

    is_cacheable_res = 0;
    cursor = cache_data;
    *cache_len = 0; // 총 응답 길이를 0으로 초기화한다.
    content_len = 0; // Content-Length를 0으로 초기화한다.

    // 응답 헤더를 읽고 클라이언트에게 전송한다.
    // 헤더의 끝('\r\n'만 있는 빈 줄)에 도달할 때까지 반복한다.
    while (strcmp(buf, "\r\n"))
    {
        Rio_readlineb(rp, buf, MAXBUF); // 서버에서 한 줄을 읽는다.

        /* get content length for readnb (본문을 읽기 위해 Content-Length 추출) */
        // Content-Length 헤더를 찾는다. (대소문자 무시)
        if (strcasestr(buf, "content-length"))
        {
            p = index(buf, ' '); // 공백을 찾는다.
            sscanf(p + 1, "%ld", &content_len); // Content-Length 값을 추출한다.
        }

        Rio_writen(fd, buf, strlen(buf)); // 클라이언트에게 헤더를 전송한다.
        *cache_len += strlen(buf); // 총 응답 길이에 헤더 길이를 더한다.

        /* if response length is cacheable size (응답 길이가 캐시 가능한 크기라면) */
        if (*cache_len < MAX_OBJECT_SIZE)
        {
            // 헤더를 캐시 버퍼에 복사한다.
            memcpy(cursor, buf, strlen(buf));
            cursor += strlen(buf);
        }
        else
        {
            is_cacheable_res = 1; // 캐시 제한 초과 플래그를 설정한다.
        }
    }

    /* Read response body and send to client (응답 본문을 읽어 클라이언트에게 전송) */
    // Content-Length 만큼의 본문을 저장할 버퍼를 생성한다.
    char body[content_len];
    Rio_readnb(rp, body, content_len); // 서버에서 본문 전체를 읽는다.
    Rio_writen(fd, body, content_len); // 클라이언트에게 본문을 전송한다.
    *cache_len += content_len; // 총 응답 길이에 본문 길이를 더한다.

    /* if response length is cacheable size (응답 길이가 캐시 가능한 크기라면) */
    if (*cache_len < MAX_OBJECT_SIZE)
    {
        // 본문을 캐시 버퍼에 복사한다.
        memcpy(cursor, body, content_len);
        cursor += content_len;
    }
    else
    {
        is_cacheable_res = 1; // 캐시 제한 초과 플래그를 설정한다.
    }
    return is_cacheable_res;
}

/** Read headers from client and send to server (클라이언트 헤더를 읽어 서버로 전송)
 * @param rp              클라이언트 소켓에 연결된 RIO 구조체 포인터이다.
 * @param fd              서버 소켓 파일 디스크립터이다.
 * @param host            서버 호스트 이름이다.
 * @param port            서버 포트 번호이다.
 * @return                void
 */
void handle_client_headers(rio_t *rp, int fd, char *host, char *port)
{
    char buf[MAXBUF];

    /* custom headers from proxy (프록시가 생성하는 커스텀 헤더) */
    // 프록시가 대신 Host, User-Agent, Connection, Proxy-Connection 헤더를 생성하여 서버로 보낸다.
    sprintf(buf, "Host: %s:%s\r\n", host, port);
    sprintf(buf, "%sUser-Agent: %s\r\n", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);
    // 버퍼에 누적된 헤더들을 서버로 한 번에 전송한다.
    Rio_writen(fd, buf, strlen(buf));

    /* original headers from client (클라이언트로부터 온 원본 헤더) */
    // 헤더의 끝('\r\n')이 나올 때까지 반복한다.
    while (strcmp(buf, "\r\n"))
    {
        Rio_readlineb(rp, buf, MAXBUF); // 클라이언트에서 헤더를 한 줄 읽는다.
        
        /* ignore some headers (일부 헤더 무시) */
        // 이미 프록시가 설정한 헤더는 클라이언트가 보낸 것을 무시한다.
        if ((strcasestr(buf, "user-agent")) || (strcasestr(buf, "connection")) ||
            (strcasestr(buf, "proxy-connection")) || (strcasestr(buf, "host")))
        {
            continue; // 무시할 헤더이면 다음 루프로 넘어간다.
        }
        Rio_writen(fd, buf, strlen(buf)); // 무시하지 않은 헤더는 서버로 전달한다.
    }
}

/** Get host, port, path from uri (URI에서 호스트, 포트, 경로를 얻는다.)
 * @param buf             URI 문자열이다.
 * @param host            호스트 이름 버퍼이다.
 * @param port            포트 번호 버퍼이다.
 * @param path            경로 버퍼이다.
 * @return                성공 시 0, 실패 시 1이다.
 */
int parse_uri(char *buf, char *host, char *port, char *path)
{
    char tmp[MAXLINE];
    char *p, *q;

    /* ignore request if not started with http:// (http://로 시작하지 않으면 요청 무시) */
    // "http://"를 제외한 나머지 부분을 tmp에 파싱한다.
    if (sscanf(buf, "http://%s", tmp) == 0)
    {
        return 1; // 파싱 실패 시 1을 반환한다.
    };

    p = index(tmp, ':'); // 콜론(:)을 찾는다. (포트 번호가 명시된 경우)
    if (p)
    { /* port exist (포트 번호가 존재한다.) */
        *p = '\0'; // 콜론 위치를 널 문자로 덮어써서 호스트 이름만 남긴다.
        strcpy(host, tmp); // 호스트 이름을 복사한다.
        q = index(p + 1, '/'); // 콜론 뒤에서 첫 번째 슬래시(/)를 찾는다. (경로 시작)
        *q = '\0'; // 슬래시 위치를 널 문자로 덮어써서 포트 번호만 남긴다.
        strcpy(port, p + 1); // 포트 번호를 복사한다.
    }
    else
    { /* default port 80 (기본 포트 80) */
        q = index(tmp, '/'); // 슬래시(/)를 찾는다.
        *q = '\0'; // 슬래시 위치를 널 문자로 덮어써서 호스트 이름만 남긴다.
        strcpy(host, tmp); // 호스트 이름을 복사한다.
        strcpy(port, "80"); // 기본 포트 80을 설정한다.
    }

    // 경로를 설정한다.
    strcpy(path, "/"); // 기본 경로를 "/"로 설정한다.
    if (q)
        strcat(path, q + 1); // 슬래시 뒤의 나머지 부분을 경로에 추가한다.
        
    return 0;
}

/** Find substring ignoring case (대소문자를 무시하고 부분 문자열을 찾는다.)
 * @param haystack        검색 대상 문자열이다.
 * @param needle          찾을 부분 문자열이다.
 * @return                부분 문자열의 시작 포인터, 없으면 NULL이다.
 */
char *strcasestr(const char *haystack, const char *needle)
{
    int size = strlen(needle);
    while (*haystack)
    {
        // strncasecmp으로 대소문자를 무시하고 size 길이만큼 비교한다.
        if (strncasecmp(haystack, needle, size) == 0)
        {
            return (char *)haystack; // 찾았으면 포인터를 반환한다.
        }
        haystack++; // 다음 문자로 이동한다.
    }
    return NULL;
}

/** Get numeric address (숫자 주소 얻기)
 * @param host            호스트 이름 문자열이다.
 * @param serv            서비스(포트) 문자열이다.
 * @param ip              숫자 IP 주소(32비트)를 저장할 포인터이다.
 * @param port            숫자 포트 번호(16비트)를 저장할 포인터이다.
 * @return                void
 */
void get_numeric_addr(char *host, char *serv, u_int32_t *ip, u_int16_t *port)
{
    int flags;
    char dotted[MAXLINE]; // 점으로 구분된 IP 주소 문자열을 저장할 버퍼이다.
    struct addrinfo *listp, *p, hints;

    /* get numeric ip address (숫자 IP 주소 얻기) */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4 주소만 요청한다.
    hints.ai_socktype = SOCK_STREAM;

    Getaddrinfo(host, NULL, &hints, &listp); // 호스트 이름으로 주소 정보를 얻는다.
    
    // 주소 정보에서 오직 숫자 형태의 호스트/서비스만 요청한다.
    flags = NI_NUMERICHOST | NI_NUMERICSERV; 
    
    // 리스트를 순회하며 유효한 숫자 IP 주소를 찾는다.
    for (p = listp; p; p = p->ai_next)
    {
        // Getnameinfo를 사용하여 IP 주소를 점으로 구분된 문자열(dotted)로 변환한다.
        Getnameinfo(p->ai_addr, p->ai_addrlen, dotted, MAXLINE, NULL, 0, flags);
        if (strlen(dotted))
            break; // IP 주소를 얻었으면 루프를 종료한다.
    }

    /* convert dotted to 4-byte ip (점 표현 IP를 4바이트 IP로 변환) */
    // inet_addr은 dotted IP를 네트워크 바이트 순서로, ntohl은 이를 다시 호스트 바이트 순서로 변환하여 *ip에 저장한다.
    *ip = ntohl(inet_addr(dotted));

    /* convert port to 2-byte short (포트 번호를 2바이트 정수로 변환) */
    *port = atoi(serv); // 서비스 문자열을 정수로 변환한다.

    Freeaddrinfo(listp); // 할당된 주소 정보 리스트를 해제한다.
}