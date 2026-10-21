
#include "cache.h"
#include "csapp.h"
#include <signal.h>
#include <stdio.h>
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);

int parse_uri(char *uri, char *request_ip, char *port, char *filename);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(char *method, char *request_ip, char *user_agent_hdr, int clientfd, char *filename);
void server_to_client(int clientfd, int fd, char *filename);
void *thread(void *vargp);

int main(int argc, char **argv) {
    int listenfd, *connfdp; // 서버소켓,클라이언트 소켓 fd
    pthread_t tid;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]); // 듣기 소켓 오픈

    // 무한 서버 루프 실행
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);                     // 반복적으로 연결 요청 접수
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 소켓 주소를 호스트 이름 및 서비스 이름으로 변환
        //(클라이언트 주소, 클라이언트 주소의 크기, 호스트 이름, 호스트 이름 버퍼 크기, 포트번호, 포트 번호 최대 크기, 추가옵션)
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_create(&tid, NULL, thread, connfdp);
    }
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * doit - 한개의 HTTP 트랜젝션 처리
 */
void doit(int fd) {

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], request_ip[MAXLINE], port[MAXLINE];
    rio_t rio;
    int clientfd;

    // 요청 라인을 읽고 분석
    Rio_readinitb(&rio, fd);           // fd의 주소값을 rio로
    Rio_readlineb(&rio, buf, MAXLINE); // 요청 라인 읽기(한줄)

    printf("Request headers:\n");
    printf("%s", buf);

    sscanf(buf, "%s %s %s", method, uri, version); // 요청 라인에서 메서드, URI, HTTP 버전 추출

    if (strstr(uri, "favicon")) {
        return;
    }

    // GET,HEAD 메소드에 대해서만 적용
    if (strcasecmp(method, "GET") != 0 && strcasecmp(method, "HEAD") != 0) {
        clienterror(fd, method, "5O1", "Not implemented", "Tiny does not implement this method");
        return;
    }

    // uri 파싱
    parse_uri(uri, request_ip, port, filename); // URI를 요청 ip, port, file이름 추출

    // 기존에 했던 요청이라면 캐시리스트에 존재하는 데이터 전송 후 캐시리스트 재정렬
    web_object *exsit_cache = find_cache(filename);
    if (exsit_cache) {
        send_cache(exsit_cache, fd);
        update_cache(exsit_cache);
        return; // 캐시리스트에서 요청에 대한 응답처리를 했으니 서버까지 요청을 전달하지 않고 종료
    }

    // 기존에 했던 요청이 아니거나 캐시리스트에서 없어진 요청이라면
    clientfd = Open_clientfd(request_ip, port);                               // 프록시 서버 소켓 생성(서버에 전송하고 받기 위한)
    read_requesthdrs(method, request_ip, user_agent_hdr, clientfd, filename); // 요청 헤더를 읽고 서버에 전송
    server_to_client(clientfd, fd, filename);                                 // 서버에게 응답을 받고 클라이언트에게 전송
    Close(clientfd);                                                          // 소켓 닫기
}
int parse_uri(char *uri, char *request_ip, char *port, char *filename) {
    /*
    uri 파싱 조건
    요청ip,filename,port,http_version
    filename과 port는 있을 수도있고 없을수도있음
    URI_examle= http://request_ip:port/path
    http://request_ip:port
    http://request_ip/

    */
    char *ip_ptr, *port_ptr, *filename_ptr;

    ip_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri + 1;
    port_ptr = strchr(ip_ptr, ':');
    filename_ptr = strchr(ip_ptr, '/');

    if (filename_ptr != NULL) {
        strcpy(filename, filename_ptr);
        *filename_ptr = '\0';
    } else
        strcpy(filename, "/");

    if (port_ptr != NULL) {
        strcpy(port, port_ptr + 1);
        *port_ptr = '\0';

    } else {
        strcpy(port, "80");
    }
    strcpy(request_ip, ip_ptr);
}

void read_requesthdrs(char *method, char *request_ip, char *user_agent_hdr, int clientfd, char *filename) {
    char buf[MAXLINE]; // 임시 버프

    /*자른 데이터를 헤더로 만들어 buf로 tiny에게 전달*/
    sprintf(buf, "%s %s %s\r\n", method, filename, "HTTP/1.0");
    sprintf(buf, "%sHost: %s\r\n", buf, request_ip);
    sprintf(buf, "%s%s", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: %s\r\n", buf, "close");
    sprintf(buf, "%sProxy-Connection: %s\r\n\r\n", buf, "close");

    // 서버로 보낸다.
    Rio_writen(clientfd, buf, strlen(buf));
}

void server_to_client(int clientfd, int fd, char *filename) {
    /*
     *서버한테 받은 응답 읽어오기
     *프록시 서버가 해당 응답을 header는 바로 보내고
     *body는 content-type의 크기만큼 한번에 모아서 보내기
     */
    char *srcp, *p, content_length[MAXLINE], buf[MAXBUF];
    rio_t response_rio;
    int content_len;

    Rio_readinitb(&response_rio, clientfd); // 초기화

    // header 전송
    while (strcmp(buf, "\r\n")) {
        if (strstr(buf, "Content-length"))
            content_len = atoi(strchr(buf, ':') + 1);

        Rio_readlineb(&response_rio, buf, MAXLINE); // 응답라인 읽기
        Rio_writen(fd, buf, strlen(buf));
    }

    // body 전송
    srcp = malloc(content_len);
    Rio_readnb(&response_rio, srcp, content_len);
    Rio_writen(fd, srcp, content_len);
    if (content_len <= MAX_OBJECT_SIZE) {
        web_object *web_ob = (web_object *)calloc(1, sizeof(web_object));
        web_ob->response_ptr = srcp;
        web_ob->content_len = content_len;
        strcpy(web_ob->filename, filename);
        regi_cache(web_ob);
    } else
        Free(srcp);
}
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    // MAXBUF : 8192
    char buf[MAXLINE], body[MAXBUF];

    /*Build the HTTP response body*/
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "ffffff"
                  ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The tiny Web server</em>\r\n", body);

    /*Print the HTTP response*/
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}