#include "csapp.h"              // CS:APP 유틸리티 함수들
#define MAX_CACHE_SIZE 1049000   // 총 캐시 용량 제한
#define MAX_OBJECT_SIZE 102400   // 개별 오브젝트 최대 크기
// 캐시 구조체
typedef struct CacheLine_struct {
    char uri[MAXLINE];            // URI 문자열
    char *object;                 // 응답 객체
    int size;                     // 객체 크기
    struct CacheLine_struct *prev, *next; // LRU 연결
} CacheLine;
// 캐시 전역 상태
CacheLine *cache_head = NULL, *cache_tail = NULL;
int current_cache_size = 0;
pthread_mutex_t mutex;
// 함수 원형
void cache_init();
int cache_find_and_send(char *uri, int fd);
void cache_uri(char *uri, char *buf, int size);
void move_to_front(CacheLine *line);
void evict_cache();
void doit(int fd);
void parse_uri(const char *uri, char *hostname, char *path, int *port);
void build_http_header(char *http_header, char *hostname, char *path, char *method, rio_t *client_rio);
void *thread(void *vargp);
// 메인 루틴
int main(int argc, char **argv)
{
    int listenfd;
    int *connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    cache_init();
    pthread_mutex_init(&mutex, NULL);
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <port>\n", argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        pthread_create(&tid, NULL, thread, connfd);
    }
    return 0;
}
// 클라이언트 요청 처리
// void doit(int fd){
//     char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
//     char hostname[MAXLINE], path[MAXLINE];
//     int server_fd, port;
//     char server_header[MAXLINE];
//     rio_t rio_client, rio_server;
//     // 클라이언트로부터 요청 라인 읽기
//     Rio_readinitb(&rio_client, fd);
//     if (Rio_readlineb(&rio_client, buf, MAXLINE) <= 0)
//         return;
//     printf("[doit] Received request: %s\n", uri);
//     fflush(stdout);
//     sscanf(buf, "%s %s %s", method, uri, version);
//     if (strcasecmp(method, "GET")) {
//         printf("Proxy does not implement this method\n");
//         return;
//     }
//     // 캐시에 있으면 전송 후 종료
//     if (cache_find_and_send(uri, fd)) return;
//     // 서버에 보낼 요청 헤더 구성
//     parse_uri(uri, hostname, path, &port);
//     build_http_header(server_header, hostname, path, method, &rio_client);
//     // 서버에 연결
//     char port_str[10];
//     sprintf(port_str, "%d", port);
//     server_fd = Open_clientfd(hostname, port_str);
//     if (server_fd < 0) {
//         printf("Connection failed\n");
//         return;
//     }
//     // 서버에 요청 전송
//     Rio_readinitb(&rio_server, server_fd);
//     Rio_writen(server_fd, server_header, strlen(server_header));
//     // 응답 헤더 먼저 처리
//     char cache_buf[MAX_OBJECT_SIZE];
//     int total_size = 0;
//     size_t n;
//     while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0) {
//         Rio_writen(fd, buf, n);
//         if (total_size + n <= MAX_OBJECT_SIZE)
//             memcpy(cache_buf + total_size, buf, n);
//         total_size += n;
//         if (!strcmp(buf, "\r\n")) break;  // 헤더 끝
//     }
//     // 응답 본문 처리
//     while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
//         Rio_writen(fd, buf, n);
//         if (total_size + n <= MAX_OBJECT_SIZE)
//             memcpy(cache_buf + total_size, buf, n);
//         total_size += n;
//     }
//     // 캐시 가능하면 저장
//     if (total_size <= MAX_OBJECT_SIZE)
//         cache_uri(uri, cache_buf, total_size);
//     Close(server_fd);
// }
// // URI 파싱
// void parse_uri(char *uri, char *hostname, char *path, int *port)
// {
//     *port = 80;
//     char *ptr = strstr(uri, "//");
//     ptr = (ptr != NULL) ? ptr + 2 : uri;
//     char *port_ptr = strstr(ptr, ":");
//     char *path_ptr = strstr(ptr, "/");
//     if (port_ptr && path_ptr && port_ptr < path_ptr) {
//         *port_ptr = '\0';
//         sscanf(ptr, "%s", hostname);
//         sscanf(port_ptr + 1, "%d%s", port, path);
//     } else {
//         if (path_ptr) {
//             *path_ptr = '\0';
//             sscanf(ptr, "%s", hostname);
//             *path_ptr = '/';
//             sscanf(path_ptr, "%s", path);
//         } else {
//             sscanf(ptr, "%s", hostname);
//             strcpy(path, "/");
//         }
//     }
// }
void parse_uri(const char *uri, char *hostname, char *path, int *port) {
    *port = 80;
    const char *pos = strstr(uri, "//");
    pos = (pos != NULL) ? pos + 2 : uri;
    const char *port_pos = strchr(pos, ':');
    const char *path_pos = strchr(pos, '/');
    if (port_pos && path_pos && port_pos < path_pos) {
        strncpy(hostname, pos, port_pos - pos);
        hostname[port_pos - pos] = '\0';
        sscanf(port_pos + 1, "%d%s", port, path);
    } else if (path_pos) {
        strncpy(hostname, pos, path_pos - pos);
        hostname[path_pos - pos] = '\0';
        strcpy(path, path_pos);
    } else {
        strcpy(hostname, pos);
        strcpy(path, "/");
    }
}
void doit(int fd){
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int server_fd, port;
    char server_header[MAXLINE];
    rio_t rio_client, rio_server;
    // 클라이언트로부터 요청 라인 읽기
    Rio_readinitb(&rio_client, fd);
    if (Rio_readlineb(&rio_client, buf, MAXLINE) <= 0)
        return;
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("[doit] Method: %s, URI: %s, Version: %s\n", method, uri, version);
    fflush(stdout);
    if (strcasecmp(method, "GET")) {
        printf("[doit] Unsupported method: %s\n", method);
        return;
    }
    // 캐시에 존재하면 전송 후 종료
    if (cache_find_and_send(uri, fd)) {
        printf("[doit] Served from cache: %s\n", uri);
        return;
    }
    // URI 파싱 및 서버 요청 헤더 생성
    parse_uri(uri, hostname, path, &port);
    printf("[doit] Parsed URI → Host: %s, Path: %s, Port: %d\n", hostname, path, port);
    fflush(stdout);
    build_http_header(server_header, hostname, path, method, &rio_client);
    // 서버에 연결 시도
    char port_str[10];
    sprintf(port_str, "%d", port);
    server_fd = Open_clientfd(hostname, port_str);
    if (server_fd < 0) {
        printf("[doit] Failed to connect to server %s:%s\n", hostname, port_str);
        return;
    }
    printf("[doit] Connected to server %s:%s\n", hostname, port_str);
    fflush(stdout);
    // 서버에 요청 전송
    Rio_readinitb(&rio_server, server_fd);
    Rio_writen(server_fd, server_header, strlen(server_header));
    printf("[doit] Sent request to server\n");
    fflush(stdout);
    // 응답 처리 및 캐시 버퍼 구성
    char cache_buf[MAX_OBJECT_SIZE];
    int total_size = 0;
    size_t n;
    // 응답 헤더 읽기
    while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
        if (total_size + n <= MAX_OBJECT_SIZE)
            memcpy(cache_buf + total_size, buf, n);
        total_size += n;
        if (!strcmp(buf, "\r\n")) break;  // 헤더 종료
    }
    // 응답 본문 읽기
    while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
        if (total_size + n <= MAX_OBJECT_SIZE)
            memcpy(cache_buf + total_size, buf, n);
        total_size += n;
    }
    printf("[doit] Total response size: %d bytes\n", total_size);
    fflush(stdout);
    // 캐시에 저장 (중복 방지)
    if (total_size <= MAX_OBJECT_SIZE) {
    if (!cache_find_and_send(uri, -1)) { // fd = -1 → 응답은 보내지 않도록 특별 처리
        cache_uri(uri, cache_buf, total_size);
        printf("[doit] Cached response for URI: %s\n", uri);
    } else {
        printf("[doit] Skipped caching: Already cached by another thread → %s\n", uri);
    }
  }
    Close(server_fd);
    printf("[doit] Connection closed for URI: %s\n", uri);
    fflush(stdout);
}
// HTTP 요청 헤더 구성
void build_http_header(char *http_header, char *hostname, char *path, char *method, rio_t *client_rio)
{
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE] = "";
    sprintf(request_hdr, "%s %s HTTP/1.0\r\n", method, path);
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n")) break;
        if (!strncasecmp(buf, "Host", 4)) {
            strcpy(host_hdr, buf);
            continue;
        }
        if (strncasecmp(buf, "User-Agent", 10) &&
            strncasecmp(buf, "Connection", 10) &&
            strncasecmp(buf, "Proxy-Connection", 16)) {
            strcat(other_hdr, buf);
        }
    }
    if (strlen(host_hdr) == 0)
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    sprintf(http_header, "%s%s%sConnection: close\r\nProxy-Connection: close\r\n\r\n",
            request_hdr, host_hdr, other_hdr);
}
// 스레드 루틴
void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    printf("[Thread %lu] Handling connection...\n", pthread_self()); // 로그 추가
    fflush(stdout); // 로그 버퍼 강제 출력
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}
// 캐시 초기화
void cache_init()
{
    cache_head = NULL;
    cache_tail = NULL;
    current_cache_size = 0;
}
// 캐시 탐색 및 전송
// int cache_find_and_send(char *uri, int fd)
// {
//     pthread_mutex_lock(&mutex);
//     CacheLine *cur = cache_head;
//     while (cur) {
//         if (strcmp(cur->uri, uri) == 0) {
//             printf("[Cache] HIT: %s\n", uri); fflush(stdout);
//             Rio_writen(fd, cur->object, cur->size);
//             move_to_front(cur);
//             pthread_mutex_unlock(&mutex);
//             return 1;
//         }
//         cur = cur->next;
//     }
//     pthread_mutex_unlock(&mutex);
//     printf("[Cache] MISS: %s\n", uri); fflush(stdout);
//     return 0;
// }
int cache_find_and_send(char *uri, int fd)
{
    pthread_mutex_lock(&mutex);
    CacheLine *cur = cache_head;
    while (cur) {
        if (strcmp(cur->uri, uri) == 0) {
            printf("[Cache] HIT: %s\n", uri); fflush(stdout);
            if (fd != -1)  // 정상 요청일 때만 응답 전송
                Rio_writen(fd, cur->object, cur->size);
            move_to_front(cur);
            pthread_mutex_unlock(&mutex);
            return 1;
        }
        cur = cur->next;
    }
    pthread_mutex_unlock(&mutex);
    printf("[Cache] MISS: %s\n", uri); fflush(stdout);
    return 0;
}
// 캐시 저장
void cache_uri(char *uri, char *buf, int size)
{
    pthread_mutex_lock(&mutex);
    while (current_cache_size + size > MAX_CACHE_SIZE)
        evict_cache();
    CacheLine *line = Malloc(sizeof(CacheLine));
    strcpy(line->uri, uri);
    line->object = Malloc(size);
    memcpy(line->object, buf, size);
    line->size = size;
    line->next = cache_head;
    line->prev = NULL;
    if (cache_head) cache_head->prev = line;
    cache_head = line;
    if (!cache_tail) cache_tail = line;
    current_cache_size += size;
    pthread_mutex_unlock(&mutex);
}
// LRU 정렬
void move_to_front(CacheLine *line)
{
    if (line == cache_head) return;
    if (line->prev) line->prev->next = line->next;
    if (line->next) line->next->prev = line->prev;
    if (line == cache_tail) cache_tail = line->prev;
    line->next = cache_head;
    line->prev = NULL;
    if (cache_head) cache_head->prev = line;
    cache_head = line;
}
// 캐시 제거 (LRU)
void evict_cache()
{
    if (!cache_tail) return;
    CacheLine *victim = cache_tail;
    if (victim->prev) {
        cache_tail = victim->prev;
        cache_tail->next = NULL;
    } else {
        cache_head = NULL;
        cache_tail = NULL;
    }
    current_cache_size -= victim->size;
    Free(victim->object);
    Free(victim);
}
