#include <stdio.h>
#include "csapp.h"
#include "cache.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
void *thread(void *vargp);

// argc : 명령줄에서 전달된 인수의 개수 예시) ./tiny 8080 이면 argc는 2
// argv : 명령줄에서 전달된 인수의 배열 argv[0]="./tiny" , argv[1]="8080"
int main(int argc, char **argv) {
  int listenfd, *connfdp;                   // listen 소켓과 클라이언트와의 연결 소켓을 저장할 변수
  socklen_t clientlen;                    // 클라이언트 소켓 주소의 길이를 저장할 변수
  struct sockaddr_storage clientaddr;     // 클라이언트의 소켓 주소를 저장할 구조체 (IPv4, IPv6 모두를 지원)
  pthread_t tid;

  // 실행 시 포트 번호가 제대로 전달되지 않으면 사용법을 출력하고 종료
  if (argc != 2) { 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);}//프로그램 종료

  listenfd = Open_listenfd(argv[1]); // 포트 번호를 사용하여 리슨 소켓을 오픈 (서버가 대기할 준비)

  // 서버는 무한 루프
  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}
void *thread(void *vargp){
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int fd) 
{
    int serverfd;
    // request line에서 메서드, URI, 버전 정보를 저장할 변수들
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE],host[MAXLINE],hostname[MAXLINE],path[MAXLINE],port[MAXLINE],server_request[MAXLINE];
    // Robust I/O 버퍼 구조체
    rio_t rio_client, rio_server;

    Rio_readinitb(&rio_client, fd);  // fd와 연관된 Robust I/O 읽기 버퍼 초기화
    if (!Rio_readlineb(&rio_client, buf, MAXLINE)) // 클라이언트로부터 요청 라인을 읽어 buf에 저장
        return;  // 읽을 내용이 없으면 함수 종료

    /* Client 요청 라인 파싱 */
    sscanf(buf, "%s %s %s", method, uri, version); // 공백으로 구분해서 요청 라인을 파싱하여 메서드, URI, 버전 저장

    // 캐시에서 요청한 URI가 있는지 확인 
    const char *cached_obj = cache_get(uri);
    if (cached_obj!=NULL){
      Rio_writen(fd,cached_obj,strlen(cached_obj));
      return;
    }

    // "http://%[^/]%s" 의 의미 : http://부터~ , %[^/] : '/'나오기 전에 문자 전부, %s : %[^/]이후에 나머지
    sscanf(uri, "http://%[^/]%s", host, path);  // host: localhost:8000, path: ''
    sscanf(host, "%[^:]:%s",hostname,port);     // hostname:localhost, port:8000
    // path가 빈 경우 /로 설정
    if (path[0] == '\0') {strcpy(path, "/"); }// 빈 문자열인 경우 '/'로 설정    
    
    /* Tiny 서버에 연결  */ 
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        fprintf(stderr, "Failed to connect to Tiny server\n");
        return;
    }

    // proxy->tiny 요청 헤더 처리
    sprintf(server_request, "%s %s HTTP/1.0\r\n", method, path);
    sprintf(server_request, "%sHost: %s\r\n", server_request, hostname);

    while (Rio_readlineb(&rio_client, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0) {
            break; // 헤더 종료
        }

        char *colon_pos = strchr(buf, ':');
        if (colon_pos) {
            *colon_pos = '\0'; // ':'를 잠시 null로 바꿈
            char key[MAXLINE];
            strcpy(key, buf);

            *colon_pos = ':'; // ':'를 다시 복구

            // Host ~ User-Agent는 아래에 sprintf로 지정된 헤더를 입력 할 것이다.
            if (strcmp(key, "Host") == 0 ||
              strcmp(key, "Connection") == 0 ||
                strcmp(key, "Proxy-Connection") == 0 ||
                strcmp(key, "User-Agent") == 0) {
                continue;
                }
            sprintf(server_request, "%s%s", server_request, buf);
        }
    }

    sprintf(server_request, "%s%s", server_request, user_agent_hdr);
    sprintf(server_request, "%sConnection: close\r\n", server_request);
    sprintf(server_request, "%sProxy-Connection: close\r\n", server_request);
    sprintf(server_request, "%s\r\n", server_request);

    // 요청을 tiny 서버로 전달
    Rio_writen(serverfd, server_request, strlen(server_request));
    // ----------------- 프록시 -> 서버 write 끝----------------

    /* Client에게 전송 */
    Rio_readinitb(&rio_server, serverfd);  // fd와 연관된 Robust I/O 읽기 버퍼 초기화

    // int readlen;
    // while((readlen = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0){      
    //   Rio_writen(fd, buf, readlen);
    //   printf("%s",buf);
    // }
    // Close(serverfd);


    size_t n;
    char object_buf[MAX_OBJECT_SIZE];
    size_t total_size = 0;
    
    // 응답을 클라이언트로 전송하면서 동시에 버퍼에 저장
    while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
        if (total_size + n < MAX_OBJECT_SIZE) {
            memcpy(object_buf + total_size, buf, n);
            total_size += n;
        }
    }

    // 캐시에 저장할 수 있는 경우
    if (total_size < MAX_OBJECT_SIZE) {
        cache_put(uri, object_buf, total_size);
    }

    Close(serverfd);
}


