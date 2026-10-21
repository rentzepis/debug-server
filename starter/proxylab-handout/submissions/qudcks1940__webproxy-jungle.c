#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);
void parse_url(char *url, char *client_hostname, char *client_port, char *client_path);
void read_requesthdrs(rio_t *rp, char *new_header);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void forward_request(int proxy_fd, char *new_header, char *uri, char *port, char *host);
void reverse_proxy(int proxy_fd, int fd);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
    
    int listenfd;
    int *clientfd;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    pthread_t tid;

    // struct sigaction sa;
    // sa.sa_handler = sigpipe_handler;
    // sigemptyset(&sa.sa_mask);
    // sa.sa_flags = 0;
    // sigaction(SIGPIPE, &sa, NULL);

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]); // opening a listening socket
    
    while (1) {
        clientlen = sizeof(clientaddr);
        clientfd = Malloc(sizeof(int));
        *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // acceptig a connection request
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0); // converting socketaddr to form that is available to read

        printf("Accepted connection from (%s, %s)\n", client_hostname, client_port);
        Pthread_create(&tid, NULL, thread, clientfd);
        // doit(clientfd);   // line:netp:tiny:doit
        // Close(clientfd);  // line:netp:tiny:close
    }
}

void *thread(void *vargp) {
  int clientfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(clientfd);
  Close(clientfd);
  return NULL;
}

// 클라이언트 요청 수신 및 파싱
// 클라이언트에서 프록시로 받는 부분은 uri가 아니라 url임!
// 요청 메서드, url, HTTP 버전을 파싱하고, 
// url에서 호스트 이름, 경로, 포트 번호를 추출
void doit(int fd) {

    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    int proxy_fd, n;
    char new_header[MAXLINE], proxy_request[MAXLINE], proxy_buf[MAXLINE];

    rio_t rio; // Robust I/O: 버퍼링된 입출력 처리

    /* Read Request line and headers */
    // fd에 client의 정보가 담겨 있으므로 rio 구조체에 초기화해준다.
    Rio_readinitb(&rio, fd);

    // rio 구조체에 담긴 정보 중 첫 번째 줄만 buf에 저장
    // Rio_readlineb의 반환값은 buf에 저장되는 길이기 때문에
    // 0이면 rio에 아무것도 없다는 뜻이므로 그냥 return하게 만든다.
    if (!Rio_readlineb(&rio, buf, MAXLINE))
        return;

    printf("Client Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);

    // 이제 url에서 호스트 이름, 포트 번호, 경로를 추출해야함.
    parse_url(url, hostname, port, path);

    printf("%s\n",hostname);
    printf("%s\n",port);
    printf("%s\n",path);

    read_requesthdrs(&rio, new_header);

    if ((strcasecmp(method, "GET")) && (strcasecmp(method, "HEAD"))){ 
        // HTTP 메서드가 GET인지 확인 (GET만 지원함, 다른 메서드는 지원하지 않음)
        clienterror(fd, method, "501", "Not implemented",
                    "Tiny does not implement this method"); 
                    // GET이 아닌 메서드일 경우 501 Not Implemented 에러를 클라이언트에 응답
        return;
    }

    proxy_fd = Open_clientfd(hostname, port);
    // 이제 server로 request를 보내야함.

    if (proxy_fd < 0) { // 연결 실패 시
        clienterror(fd, hostname, "404", "Not found", "Proxy couldn't connect to the server"); // 클라이언트에게 오류 응답 전송
        return;
    }

    forward_request(proxy_fd, new_header, path, port, hostname);
    reverse_proxy(proxy_fd, fd);

}

void forward_request(int proxy_fd, char *new_header, char *uri, char *port, char *host)
{
    rio_t proxy_rio;
    char proxy_request[MAXLINE];
    Rio_readinitb(&proxy_rio, proxy_fd);
    snprintf(proxy_request, MAXLINE,
             "GET %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "%s"
             "Connection: close\r\n"
             "Proxy-Connection: close\r\n"
             "%s",
             uri, host, user_agent_hdr, new_header);
    Rio_writen(proxy_fd, proxy_request, strlen(proxy_request));
    printf("Proxy Request Headers:\r\n");
    printf("%s", proxy_request);
}

void reverse_proxy(int proxy_fd, int fd)
{
    int n;
    rio_t proxy_rio;
    char proxy_buf[MAXLINE], proxy_request[MAXLINE];
    printf("Proxy Response Headers:\r\n");
    Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE);
    printf("proxy buf %s", proxy_buf);
    Rio_writen(fd, proxy_buf, strlen(proxy_buf));
    while (strcmp(proxy_buf, "\r\n"))
    {
        Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE);
        printf("%s", proxy_buf);
        Rio_writen(fd, proxy_buf, strlen(proxy_buf));
    }
    while ((n = Rio_readlineb(&proxy_rio, proxy_buf, MAXLINE)) != 0)
    {
        Rio_writen(fd, proxy_buf, n);
    }
}

/* Parse an HHTP url */
void parse_url(char *url, char *client_hostname, char *client_port, char *client_path) {
    char *ptr;
    
    // 기본 포트를 설정 (HTTP의 경우 80번 포트)
    strcpy(client_port, "80");
    
    // URL에서 "http://" 혹은 "https://"를 제거
    if (strncmp(url, "http://", 7) == 0) {
        url += 7;  // "http://" 스킵
    } else if (strncmp(url, "https://", 8) == 0) {
        url += 8;  // "https://" 스킵
        strcpy(client_port, "443"); // HTTPS 기본 포트
    }
    
    // URL에서 첫 번째 '/'를 찾아서 호스트와 경로 분리
    ptr = strchr(url, '/');
    if (ptr) {
        strcpy(client_path, ptr);  // '/' 포함한 경로 복사
        *ptr = '\0';  // 경로를 제거하고 호스트만 남김
    } else {
        strcpy(client_path, "/");  // 경로가 없으면 기본 경로 '/'
    }
    
    // URL에 ':'가 있는지 확인 (포트 번호가 있는 경우)
    ptr = strchr(url, ':');
    if (ptr) {
        *ptr = '\0';  // ':' 이전까지는 호스트
        strcpy(client_hostname, url);  // 호스트 저장
        strcpy(client_port, ptr + 1);  // ':' 이후는 포트 번호
    } else {
        strcpy(client_hostname, url);  // 포트가 없으면 호스트만 복사
    }

    printf("Parsed URL -> Host: %s, Port: %s, Path: %s\n", client_hostname, client_port, client_path);
}

void read_requesthdrs(rio_t *rp, char *new_header)
{
    char buf[MAXLINE];
    Rio_readlineb(rp, buf, MAXLINE);
    while (strcmp(buf, "\r\n"))
    {
        char *header_key = strtok(buf, ":");
        char *header_value = strtok(NULL, "");

        if (header_key != NULL && header_value != NULL)
        {
            while (*header_value == ' ')
                header_value++;
            if (strcasecmp(header_key, "Host") == 0)
            {
                continue;
            }
            else if (strcasecmp(header_key, "User-Agent") == 0)
            {
                continue;
            }
            else if (strcasecmp(header_key, "Connection") == 0)
            {
                continue;
            }
            else if (strcasecmp(header_key, "Proxy-Connection") == 0)
            {
                continue;
            }
            sprintf(new_header + strlen(new_header), "%s: %s\r\n", header_key, header_value);
            printf("Header Key: %s\n", header_key);
            printf("Header Value : %s\n", header_value);
        }
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    sprintf(new_header + strlen(new_header), "\r\n");
}

// send an HTTP response to the client with the appropriate status code and status message
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}