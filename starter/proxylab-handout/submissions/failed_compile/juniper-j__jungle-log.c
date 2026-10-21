#include <stdio.h>
#include <pthread.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000      // ?
#define MAX_OBJECT_SIZE 102400      // ?
#define DEFAULT_PORT "80"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/************************************************
 * 함수 선언부
 ************************************************/
void *thread(void *vargp);
void handle_client(int clientfd);
void read_requesthdrs(rio_t *request_rio, const char *uri, char *host_line, char *other_hdrs);
void parse_uri(const char *uri, char *hostname, char *port, char *path);
void reassemble_request(char *request, char *path, char *hostname, char *other_header);
void forward_response(int serverfd, int fd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);


/************************************************
 * main - Proxy 웹서버의 진입점
 *
 * 인자:
 *   argc: 명령행 인자의 개수
 *   argv: 명령행 인자의 문자열 배열 (argv[1]은 포트 번호)
 *
 * 반환:
 *   없음 (무한 루프 실행)
 *
 * 기능:
 *   - 지정한 포트 번호로 리스닝 소켓을 열고
 *   - 클라이언트 요청을 반복적으로 수락
 *   - 각 요청을 스레드로 처리 (병렬 처리)
 ************************************************/
int main(int argc, char **argv)
{
    int listenfd;                                // listenfd: 서버 리스닝 소켓
    char cl_hostname[MAXLINE], cl_port[MAXLINE]; // 클라이언트의 호스트 이름과 포트 번호를 저장할 버퍼
    socklen_t clientlen;                         // 클라이언트 주소 구조체의 크기
    struct sockaddr_storage clientaddr;          // 클라이언트의 주소 정보를 저장 (IPv4/IPv6 호환)

    // [1] 명령행 인자가 올바르게 주어졌는지 확인 (포트 번호 하나가 필수)
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]); // 사용법 안내
        exit(1);                                        // 포트 인자가 없으면 프로그램 종료
    }

    // [2] 주어진 포트 번호로 서버 리스닝 소켓 생성
    listenfd = Open_listenfd(argv[1]);

    // [3] 무한 루프: 클라이언트의 요청을 반복적으로 수락하고 처리
    while (1) {
        clientlen = sizeof(clientaddr);                 // clientaddr 구조체 크기 초기화
        int *connfdp = malloc(sizeof(int));             // *thread()에 전달할 clientfd 동적 할당

        // [3-1] 클라이언트 연결 수락 → 새로 연결된 소켓 clientfd 획득
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // [3-2] 클라이언트 주소 정보를 사람이 읽을 수 있는 문자열로 변환 (로그용)
        Getnameinfo((SA *)&clientaddr, clientlen,
                    cl_hostname, MAXLINE,
                    cl_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", cl_hostname, cl_port);

        // [3-3] 요청을 처리할 스레드 생성
        pthread_t tid;
        Pthread_create(&tid, NULL, thread, connfdp);    // *thread()에 clientfd 전달
    }
}


/************************************************
 * thread - 스레드로 실행되는 함수
 *
 * 인자:
 *   vargp: 클라이언트 소켓 fd를 가리키는 포인터
 *
 * 반환:
 *   없음 (void *)
 *
 * 기능:
 *   - 스레드를 detach 상태로 설정 (리소스 누수 방지)
 *   - clientfd 메모리 해제
 *   - 클라이언트 요청 처리 후 소켓 닫기
 ************************************************/`
void *thread(void *vargp) 
{
    int clientfd = *((int *)vargp);     // clientfd(클라이언트 연결 소켓 fd) 추출 
    Pthread_detach(pthread_self());     // detach 상태 설정 (join 불필요, 자동 회수)
    free(vargp);                        // main에서 malloc한 clientfd 메모리 해제

    handle_client(clientfd);            // 프록시 핵심 로직 수행
    Close(clientfd);                    // 처리 후 소켓 닫기
    return NULL;
}


/************************************************
 * handle_client - 클라이언트의 요청을 파싱하고 서버로 전달
 *
 * 인자:
 *   clientfd: 클라이언트 소켓 디스크립터
 *
 * 반환:
 *   없음
 *
 * 기능:
 *   - 요청 라인 및 헤더 파싱
 *   - URI에서 서버 주소(hostname), 포트, 경로(path) 추출
 *   - 서버와 연결하여 요청 전달
 *   - 서버 응답을 클라이언트에 전송
 *   - !!! 프록시에서는 정적/동적 판단은 필요 없음 !!!
 ************************************************/
void handle_client(int clientfd)
{   
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // 요청 라인 저장용 버퍼들
    char host_line[MAXLINE], other_hdrs[MAXLINE];           // Host 헤더와 기타 헤더 저장용 버퍼
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];  // URI에서 추출할 hostname, port, path 저장 버퍼 
    int serverfd;                       // end server와의 연결 소켓 디스크립터
    char request_buf[MAXLINE];          // 최종적으로 서버에 보낼 요청 메시지 버퍼
    rio_t request_rio;                  // Robust I/O를 위한 구조체

    // [1] 클라이언트가 보낸 요청 라인 읽기 (예: GET /index.html HTTP/1.0)
    Rio_readinitb(&request_rio, clientfd);          // 클라이언트 소켓을 Robust I/O와 연결
    if (!Rio_readlineb(&request_rio, buf, MAXLINE)) // 요청 라인이 없으면 (클라이언트가 종료했으면) 함수 종료
        return;                                       
    printf("==== Request Line ====\n%s", buf);      // 요청 라인 출력 (디버깅용)
    sscanf(buf, "%s %s %s", method, uri, version);  // 요청 라인을 method, uri, version으로 파싱
  
    // [2] GET 또는 HEAD가 아닌 메서드는 거부
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }
  
    // [3] 요청 헤더 파싱 및 Host, 기타 헤더 분리
    read_requesthdrs(&request_rio, uri, host_line, other_hdrs);

    // [4] URI를 hostname, port, path로 분해
    parse_uri(uri, hostname, port, path);

    // [5] end server와 연결 시도
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(clientfd, hostname, "502", "Bad Gateway", "Failed to connect to server");
        return;
    }

    // [6] 요청 메시지 조립 및 전송
    reassemble_request(request_buf, path, hostname, other_hdrs);    // 요청 메시지 생성
    Rio_writen(serverfd, request_buf, strlen(request_buf));         // 서버에 요청 메시지 전송

    // [7] 서버 응답을 읽어 클라이언트에 전달
    forward_response(serverfd, clientfd);                           // 서버 → 프록시 → 클라이언트
}


/************************************************
 * read_requesthdrs - 요청 헤더를 읽고 필요한 헤더만 유지
 *
 * 인자:
 *   request_rio: robust I/O 구조체 포인터 (요청 읽기 대상)
 *   uri: 요청 URI (Host 헤더가 없을 경우 보완 용도)
 *   host_line: "Host: ..." 형태의 헤더 저장 버퍼
 *   other_hdrs: 나머지 유지할 헤더 누적 버퍼
 *
 * 반환:
 *   없음
 *
 * 기능:
 *   - 요청 헤더를 \r\n(빈 줄)까지 한 줄씩 읽는다
 *   - "Host:" 헤더는 따로 저장
 *   - 기본 헤더(User-Agent, Connection, Proxy-Connection)는 필터링
 *   - 그 외 헤더는 모두 other_hdrs에 누적
 *   - Host 헤더가 없을 경우 URI로부터 생성하여 추가
 ************************************************/
void read_requesthdrs(rio_t *request_rio, const char *uri, char *host_line, char *other_hdrs)
{ 
    char buf[MAXLINE];        // 한 줄씩 임시로 저장할 버퍼
    host_line[0] = '\0';      // host 헤더를 저장할 문자열을 null로 초기화
    other_hdrs[0] = '\0';     // 나머지 헤더를 저장할 문자열을 null로 초기화 (❌ 포인터가 아니므로 'NULL'이 아닌 '\0'으로 해줘야 함)
    
    // [1] 요청 헤더를 한 줄씩 읽음 → 빈 줄("\r\n")이 나오면 종료
    while((Rio_readlineb(request_rio, buf, MAXLINE) > 0) && (strcmp(buf, "\r\n")))
    {   /* 
          매 루프마다 request_rio로부터 한 줄을 읽어 buf에 저장하고, 읽은 바이트 수가 0보다 크면 계속 반복
          더 이상 읽을게 없거나, "\r\n" 빈 줄이 나오면 반복 종료
        */
        
        // (1) Host 헤더가 있으면 별도로 저장
        if (!strncasecmp(buf, "Host:", 5)) {
            strcpy(host_line, buf);
        }
        // (2) 프록시가 직접 재구성할 기본 헤더는 제외
        else if (!strncasecmp(buf, "User-Agent:", 11) ||
                !strncasecmp(buf, "Connection:", 11) ||
                !strncasecmp(buf, "Proxy-Connection:", 17)) {
            continue;   // 우리가 재구성 할 기본 헤더는 무시
        }
        // (3) 나머지는 그대로 other_hdrs에 누적
        else {
            strcat(other_hdrs, buf);
        }
    }

    // [2] Host 헤더가 없는 경우 → URI에서 hostname과 port를 추출해 생성
    if (host_line[0] == '\0') {
        char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
        parse_uri(uri, hostname, port, path);                   // URI → hostname, port 추출
        sprintf(host_line, "Host: %s:%s\r\n", hostname, port);  // 헤더 형식으로 조립
    }
}   


/************************************************
 * parse_uri - URI 문자열을 파싱하여 서버 정보 추출
 *
 * 인자:
 *   uri: 요청 URI (http://hostname:port/path)
 *   hostname: 추출된 호스트명 저장 버퍼
 *   port: 추출된 포트 번호 저장 버퍼
 *   path: 요청 경로 저장 버퍼
 *
 * 반환:
 *   없음
 *
 * 기능:
 *   - "http://" 이후 문자열에서 호스트, 포트, 패스를 추출
 *   - 포트가 명시되지 않은 경우 기본값 80 사용
 ************************************************/
void parse_uri(const char *uri, char *hostname, char *port, char *path)
{   // e.g. GET http://www.cmu.edu:8000/hub/index.html HTTP/1.1

    char uri_copy[MAXLINE];                 // URI 원본을 훼손하지 않기 위해 복사본 생성
    char *host_start, *port_start, *path_start;

    strncpy(uri_copy, uri, MAXLINE - 1);    // strcpy는 overflow 위험이 있어 strncpy로 교체
    uri_copy[MAXLINE - 1] = '\0';           // MAXLINE - 1은 null 종료 보장을 명시적으로 함
    uri_copy[MAXLINE - 1] = '\0';

    // "http://" 또는 "https://" 제거
    host_start = strstr(uri_copy, "//");
    host_start = (host_start != NULL) ? host_start + 2 : uri_copy;

    path_start = strchr(host_start, '/');   // '/': path 시작 위치
    port_start = strchr(host_start, ':');   // ':' → port 존재 여부 확인

    // [1] path 설정
    if (path_start)                         // 경로가 명시된 경우
        strcpy(path, path_start);           // e.g. path = "/hub/index.html"
    else 
        strcpy(path, "/");                  // 요청 URI에 path가 없을 때, path = "/"

    // [2] hostname & port 설정
    if (port_start && (!path_start || port_start < path_start)) {
        // (1) 포트가 명시된 경우 → hostname = host_start ~ port_start
        int host_len = port_start - host_start;
        strncpy(hostname, host_start, host_len);
        hostname[host_len] = '\0';

        if (path_start) {
            int port_len = path_start - port_start - 1;
            strncpy(port, port_start + 1, port_len);
            port[port_len] = '\0';
        } else {
            strcpy(port, port_start + 1); // path가 없는 경우
        }
    } else {
        // (2) ':'가 없거나 path가 먼저 나오는 경우 → hostname = host_start ~ path_start
        if (path_start) {
            int host_len = path_start - host_start;
            strncpy(hostname, host_start, host_len);
            hostname[host_len] = '\0';
        } else {
            strcpy(hostname, host_start);  // path도 없음 → 전체가 hostname
        }
        strcpy(port, DEFAULT_PORT); // 기본 포트
    }
}   


/************************************************
 * reassemble_request - 서버에 보낼 요청 메시지를 구성
 *
 * 인자:
 *   request: 최종 요청 메시지를 저장할 버퍼
 *   path: 요청 URI에서 추출한 경로 (e.g. "/index.html")
 *   hostname: Host 헤더에 사용될 호스트 이름
 *   other_hdrs: 사용자 요청 중 유지할 기타 헤더 문자열
 *
 * 반환:
 *   없음
 *
 * 기능:
 *   - HTTP/1.0 방식의 요청 메시지를 조립한다
 *   - 필수 헤더(Host, User-Agent, Connection, Proxy-Connection)를 포함
 *   - 기타 유지할 헤더(other_header)를 삽입
 *   - 마지막에 빈 줄(\r\n)을 추가해 헤더 종료 표시
 ************************************************/
void reassemble_request(char *request, char *path, char *hostname, char *other_hdrs)
{
    sprintf(request,
        "GET %s HTTP/1.0\r\n"                // 요청 라인 (HTTP/1.0 사용)
        "Host: %s\r\n"                       // Host 헤더
        "%s"                                 // User-Agent 헤더 (전역 상수로 고정)
        "Connection: close\r\n"              // 클라이언트와의 연결 종료 명시
        "Proxy-Connection: close\r\n"        // 프록시와의 연결 종료 명시
        "%s"                                 // 기타 클라이언트가 보낸 유효한 헤더들
        "\r\n",                              // 헤더 종료를 의미하는 빈 줄
        path,
        hostname,
        user_agent_hdr,
        other_hdrs
    );
    /* 
    e.g. GET /hub/index.html HTTP/1.0
         Host: www.cmu.edu
         User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3
         Connection: close
         Proxy-Connection: close
         Accept: text/html
         Accept-Encoding: gzip
    */
}


/************************************************
 * forward_response - 서버 응답을 클라이언트에게 전달
 *
 * 인자:
 *   serverfd: 서버와의 연결을 나타내는 소켓 디스크립터
 *   clientfd: 클라이언트와의 연결을 나타내는 소켓 디스크립터
 *
 * 반환:
 *   없음
 *
 * 기능:
 *   - 서버로부터 응답을 읽어 클라이언트에게 그대로 전달
 *   - Robust I/O를 사용하여 줄 단위로 응답을 읽고 전송
 *   - 응답 전체를 모두 전달한 후 함수 종료
 ************************************************/
void forward_response(int serverfd, int clientfd)
{
    rio_t response_rio;                         // 서버 응답을 읽기 위한 robust I/O 구조체
    char response_buf[MAXLINE];                 // 응답 한 줄을 저장할 버퍼

    Rio_readinitb(&response_rio, serverfd);     // servedf 소켓을 robust I/O 구조체와 연결
    ssize_t n;
    
    // 서버로부터 한 줄씩 응답을 읽고, 클라이언트로 그대로 전송
    while ((n = Rio_readlineb(&response_rio, response_buf, MAXLINE)) > 0) {
        Rio_writen(clientfd, response_buf, n);  // 읽은 바이트 수 만큼 클라이언트에 전송
    }
}


/************************************************
 * get_filetype - 파일 이름에서 MIME 타입 추정
 *
 * 인자:
 *   filename: 요청된 파일 경로
 *   filetype: MIME 타입을 저장할 버퍼
 *
 * 반환:
 *   없음
 *
 * 기능:
 *   - 파일 확장자(.html, .png 등)에 따라 적절한 MIME 타입 설정
 ************************************************/
void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html")) // 파일 이름에 ".html" 포함 → HTML 문서
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif")) // GIF 이미지
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png")) // PNG 이미지
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg")) // JPEG 이미지 (.jpg)
        strcpy(filetype, "image/jpeg");
    else if (strstr(filename, ".mp4")) // ← [HW 11.7] MPG 비디어 파일 처리 (최신 브라우저는 MPG 재생 지원하지 않으므로 MP4로 대체 재생해보기)
        strcpy(filetype, "video/mp4");
    else                                // 확장자가 없거나 위에 포함되지 않음 → 일반 텍스트 파일로 간주
        strcpy(filetype, "text/plain"); // 기본값
}


/************************************************
 * clienterror - 클라이언트에 HTML 형식 오류 메시지 전송
 *
 * 인자:
 *   fd: 클라이언트 소켓 디스크립터
 *   cause: 오류 발생 원인
 *   errnum: HTTP 상태 코드 (예: "404")
 *   shortmsg: 짧은 오류 메시지 (예: "Not found")
 *   longmsg: 상세 설명 메시지
 *
 * 반환:
 *   없음
 *
 * 기능:
 *   - 오류 상태코드, 메시지를 담은 HTML 응답 생성 후 클라이언트에 전송
 ************************************************/
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE]; // 헤더 작성용 버퍼
    char body[MAXBUF]; // HTML 본문 버퍼
    char *p = body;
    int n, remaining = sizeof(body);

    // [1] HTML 본문 작성
    n = snprintf(p, remaining, "<html><title>Tiny Error</title>");
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "<body bgcolor=\"ffffff\">\r\n");
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "%s: %s\r\n", errnum, shortmsg); // ex: 404 Not Found
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "<p>%s: %s\r\n", longmsg, cause);
    p += n;
    remaining -= n;

    n = snprintf(p, remaining, "<hr><em>The Tiny Web server</em>\r\n");
    p += n;
    remaining -= n;

    // [2] HTTP 응답 헤더 전송
    snprintf(buf, sizeof(buf), "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));

    snprintf(buf, sizeof(buf), "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));

    snprintf(buf, sizeof(buf), "Content-length: %ld\r\n\r\n", strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    // [3] HTML 본문 전송
    Rio_writen(fd, body, strlen(body));
}