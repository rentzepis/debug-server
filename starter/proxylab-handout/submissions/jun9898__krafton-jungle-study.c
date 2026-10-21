#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct node {
    char *key;              // URI 키
    unsigned char *value;   // 응답 본문
    struct node *prev;      // 캐시 내 이전 노드를 가리키는 포인터
    struct node *next;      // 캐시 내 다음 노드를 가리키는 포인터
    long size;              // 응답 본문의 크기
} cache_node;

typedef struct cache {
    cache_node *root;
    cache_node *tail;
    int size;
} cache;


void doit(int clientfd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *rp);
void *thread(void *vargp);
void init_cache();
cache_node *find_cache(cache *c, char *key);
cache_node *create_node(char *key, char *value, long size);
void insert_cache(cache *c, char *key, char *value, long size);
void delete_cache(cache *c, cache_node *node);


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static cache *my_cache;

int main(int argc, char **argv) {
    int listenfd, *clientfd;                                                               // 요청을 보내는 클라이언트의 fd를 얻을거임
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);                                                    // 프록시 listenfd 열어주기
    init_cache();

    while (1) {
        clientlen = sizeof(clientaddr);
        clientfd = Malloc(sizeof(int));
        *clientfd = Accept(listenfd, (SA * ) &clientaddr, &clientlen); 
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, clientfd);
    }
}

void *thread(void *vargp) {
    int clientfd = *((int *) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(clientfd);
    Close(clientfd);
    return NULL;
}


void init_cache() {
    my_cache = (cache *) malloc(sizeof(cache));
    my_cache->root = NULL;
    my_cache->tail = NULL;
    my_cache->size = 0;
}


cache_node *create_node(char *key, char *value, long size) {
    cache_node *new_node = (cache_node *) malloc(sizeof(cache_node));
    new_node->key = malloc(strlen(key) + 1);
    strcpy(new_node->key, key);
    new_node->value = malloc(size);
    memcpy(new_node->value, value, size);
    new_node->size = size;
    new_node->prev = NULL;
    new_node->next = NULL;
    return new_node;
}


cache_node *find_cache(cache *c, char *key) {
    cache_node *current = c->root;
    while (current != NULL) {
        if (strcasecmp(current->key, key) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}


void insert_cache(cache *c, char *key, char *value, long size) {
    while (c->size + size > MAX_CACHE_SIZE) {
        delete_cache(c, c->tail);
    }
    cache_node *new_node = create_node(key, value, size);
    if (c->root == NULL) {
        c->root = new_node;
    } else {
        new_node->next = c->root;
        c->root->prev = new_node;
        c->root = new_node;
    }
    c->size += size;
}


void delete_cache(cache *c, cache_node *node) {
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        c->root = node->next;
    }
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        c->tail = node->prev;
    }
    c->size -= node->size;
    free(node->key);
    free(node->value);
    free(node);
}


void doit(int clientfd) { 
    int serverfd;                                                                       // End server와의 소켓도 연결해야 하기 때문에 fd를 저장할 int 자료형 초기화
    char request_buf[MAXLINE], response_buf[MAX_OBJECT_SIZE];                           // client의 요청과 server의 response를 저장할 버퍼 선언                
    char method[MAXLINE], uri[MAXLINE], path[MAXLINE];                 
    char hostname[MAXLINE], port[MAXLINE];                                           
    rio_t request_rio, response_rio;                                                    // request rio, response rio 설정

    Rio_readinitb(&request_rio, clientfd);                                              // client의 request를 전달할 rio 세팅
    Rio_readlineb(&request_rio, request_buf, MAXLINE);                                  // 읽어서 buf에 저장할거임
    printf("Request header: %s\n", request_buf);

    sscanf(request_buf, "%s %s", method, uri);                                          // 나눠서 각각 검사할거임

    if (!strcasecmp(uri, "/favicon.ico"))
        return;

    cache_node *cached_node = find_cache(my_cache, uri);

    if (cached_node != NULL) {
        Rio_writen(clientfd, cached_node->value, cached_node->size);
        return;
    }

    parse_uri(uri, hostname, port, path);                                               // uri에서 호스트명, 포트, 경로를 파싱

    sprintf(request_buf, "%s /%s %s\r\n", method, path, "HTTP/1.0");                    // 요청 재구성
    sprintf(request_buf, "%sConnection: close\r\n", request_buf);                       // 재수성된 요청 바탕으로 다시 헤더 작성
    sprintf(request_buf, "%sProxy-Connection: close\r\n", request_buf); 
    sprintf(request_buf, "%s%s\r\n", request_buf, user_agent_hdr); 

    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    serverfd = Open_clientfd(hostname, port);                                           // end server 와의 소켓 연결
    if (serverfd < 0) {
        clienterror(clientfd, hostname, "404", "Not found", "Proxy couldn't connect to the server");
        return;
    }

    rio_writen(serverfd, request_buf, strlen(request_buf));                             // 서버에 요청!
    Rio_readinitb(&response_rio, serverfd);                                             // 서버의 요청을 받을 response_rio 세팅

    ssize_t response_size = Rio_readnb(&response_rio, response_buf, MAX_OBJECT_SIZE);
    Rio_writen(clientfd, response_buf, response_size);

    if (strlen(response_buf) < MAX_OBJECT_SIZE) {
        insert_cache(my_cache, uri, response_buf, response_size);
    }

    Close(serverfd);
}



void parse_uri(char *uri, char *hostname, char *port, char *path) {
    char uri_copy[MAXLINE];
    strcpy(uri_copy, uri);
    char *hostname_ptr = strstr(uri_copy, "//") != NULL ? strstr(uri_copy, "//") + 2 : uri_copy + 1;
    char *port_ptr = strstr(hostname_ptr, ":");
    char *path_ptr = strstr(hostname_ptr, "/");

    // 경로가 존재한다면
    if (path_ptr > 0) {
        *path_ptr = '\0';
        strcpy(path, path_ptr + 1);
    }
    // 포트가 존재한다면
    if (port_ptr > 0) {
        *port_ptr = '\0';
        strcpy(port, port_ptr + 1);
    }
    strcpy(hostname, hostname_ptr);
}
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy server</em>\r\n", body);
    
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int) strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}


void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    // HTTP 헤더를 읽어들임
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) { // 빈 줄이 나올 때까지 반복
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf); // 헤더를 화면에 출력하거나 다른 작업을 수행할 수 있음
    }
    return;
}