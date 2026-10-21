#include "cache.h"
#include "csapp.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
                                    "Firefox/10.0.3\r\n";

/* Extra Guide */
#define PROXY_HTTP_VER "HTTP/1.0"

void update_resource_server_info(char *uri, char *path, char *hostname, char *port);
void action(int client_proxy_fd);
void *thread(void *vargp);

DLL *cache_list; // doubly linked list

int main(int argc, char *argv[]) {
    cache_list = createDoublyLinkedList();
    int listenfd = Open_listenfd(argv[1]);
    int *connfdp;
    pthread_t tid;

    struct sockaddr_storage clientaddr;
    socklen_t clientlen;
    while (1) {
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp); // [concurrency 대응]
    }

    return 0;
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    action(connfd);
    Close(connfd);
    return NULL;
}

void action(int client_proxy_fd) {
    char method[MAXBUF], uri[MAXBUF], version[MAXBUF];
    char hostname[MAXBUF], path[MAXBUF], port[MAXBUF];

    printf("======================\n[Client >> Proxy]\n======================\n");
    rio_t client_rio;
    Rio_readinitb(&client_rio, client_proxy_fd);

    // [req] client --(here)-> proxy > tiny
    char client_request[MAXBUF];
    Rio_readlineb(&client_rio, client_request, MAXBUF); // 시작라인 parsing
    sscanf(client_request, "%s %s %s\r\n", method, uri, version);

    // valid request 여부 확인
    if (method[0] == '\0' || uri[0] == '\0' || version[0] == '\0') {
        response_clienterror(client_proxy_fd, method, "400", "Bad Request", "Incomplete request: Method, URI, or Version missing");
        return;
    } else if (strcasecmp(method, "GET")) {
        response_clienterror(client_proxy_fd, method, "501", "Not implemented", "NOT valid request: Allow only [GET] method");
        return;
    }

    update_resource_server_info(uri, path, hostname, port);

    printf("======================\n[Proxy Cache 확인]\n======================\n");
    RequestInfo req_info;
    strcpy(req_info.method, method);
    strcpy(req_info.path, path);

    CacheNode *cache_node = search(cache_list, req_info);
    if (cache_node != NULL) {
        printf("======================\n[Cache Hit]\n======================\n");
        char buf[MAXBUF];

        // [res] client <-(here)-- proxy < tiny
        // header 생성 및 전달
        sprintf(buf, "%s 200 OK\r\n", PROXY_HTTP_VER); // 시작 라인
        Rio_writen(client_proxy_fd, buf, strlen(buf));

        sprintf(buf, "Content-length: %d\r\n", cache_node->res.body_size);
        Rio_writen(client_proxy_fd, buf, strlen(buf));

        Rio_writen(client_proxy_fd, "Proxy-Connection: close\r\n", strlen("Proxy-Connection: close\r\n"));
        Rio_writen(client_proxy_fd, "\r\n", strlen("\r\n")); // 헤더 끝 표시

        // body 전달
        Rio_writen(client_proxy_fd, cache_node->res.body, cache_node->res.body_size);
        moveFront(cache_list, cache_node);
        return;
    }

    printf("======================\n[Cache Miss]]\n======================\n");
    printf("======================\n[Proxy >> Tiny]\n======================\n");

    int proxy_tiny_fd = Open_clientfd(hostname, port);
    if (proxy_tiny_fd == -1) {
        printf("Tiny 서버가 꺼졌어요. 확인해주세요.\n");
        return;
    }

    // [req] client > proxy --(here)-> tiny
    char http_request_line[MAXBUF];
    sprintf(http_request_line, "%s %s %s\r\n", method, path, version);
    Rio_writen(proxy_tiny_fd, http_request_line, strlen(http_request_line));
    printf("[req] %s", http_request_line);

    int readn;
    while ((readn = Rio_readlineb(&client_rio, client_request, MAXBUF)) > 0) { // client --(req)-> proxy > tiny
        Rio_writen(proxy_tiny_fd, client_request, readn);                      // client > proxy --(req)-> tiny
        if (strcmp(client_request, "\r\n") == 0) {
            break;
        }
        printf("[req] %s", client_request);
    }

    printf("======================\n[Client << Proxy << Tiny]\n======================\n");

    rio_t proxy_rio;
    Rio_readinitb(&proxy_rio, proxy_tiny_fd);

    int content_length = 0;
    char response_header[MAXBUF]; // TODO: 응답 CacheNode에 저장하기

    // Read header
    while ((readn = Rio_readlineb(&proxy_rio, response_header, MAXBUF)) > 0) { // client > proxy <-(res)-- tiny
        Rio_writen(client_proxy_fd, response_header, readn);                   // client <-(res)-- proxy < tiny
        printf("[res] %s", response_header);

        if (strcmp(response_header, "\r\n") == 0) { // 헤더 끝 확인
            break;
        }

        if (strncmp(response_header, "Content-length:", 15) == 0) { // [추출] body 끝 확인용
            sscanf(response_header, "Content-length: %d\r\n", &content_length);
        }
    }

    // Read body
    char *response_body = (char *)malloc(content_length);
    Rio_readnb(&proxy_rio, response_body, content_length);
    Rio_writen(client_proxy_fd, response_body, content_length); // client <-(res)-- proxy < tiny
    printf("[res] %s\n", response_body);
    printf("======================\n[THE END]\n======================\n");

    // 크기 확인 후 캐시 객체 추가
    if (content_length <= MAX_OBJECT_SIZE) {
        ResponseInfo res_info;
        res_info.body_size = content_length;
        res_info.body = response_body;
        pushFront(cache_list, req_info, res_info);
    }

    free(response_body);
}

void update_resource_server_info(char *uri, char *path, char *hostname, char *port) {
    // 포트가 있을 경우만 포트 추출
    if (strstr(uri, ":")) {
        sscanf(uri, "http://%99[^:]:%9[^/]/", hostname, port);
    } else {
        sscanf(uri, "http://%99[^/]/", hostname);
    }
    sscanf(uri, "http://%*[^/]%s", path); // TODO: path (/ 없을때 기본값) 설정 필요함
    printf("[update_tiny_info] hostname=%s, port=%s, path=%s\n", hostname, port, path);
}

void response_clienterror(int fd, char *cause, char *status_code, char *shortmsg, char *longmsg) {
    char res_header[MAXLINE], body[MAXBUF];

    // Build HTTP 응답 body
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body,
            "%s<body bgcolor="
            "ffffff"
            ">\r\n",
            body);
    sprintf(body, "%s%s: %s\r\n", body, status_code, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    // Build HTTP 응답 res_header
    sprintf(res_header, "HTTP/1.0 %s %s\r\n", status_code, shortmsg);
    Rio_writen(fd, res_header, strlen(res_header));
    sprintf(res_header, "Content-type: text/html\r\n");
    sprintf(res_header, "Content-length: %d\r\n\r\n", (int)strlen(body)); // body 먼저 생성 필요

    /**
     * 응답 결과 확인 방법
     * res_header: [개발자도구] > [네트워크] > [Headers]
     * body : [개발자도구] > [네트워크] > [Response]
     * */
    Rio_writen(fd, res_header, strlen(res_header));
    Rio_writen(fd, body, strlen(body));
}
