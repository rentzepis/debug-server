#include "csapp.h"
#include "sbuf.h"

#define NTHREADS 4
#define SBUFSIZE 16

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1024 * 1024 * 1024 * 5 /* 5 MB */
#define MAX_OBJECT_SIZE 102400

#define MIN(a, b) ((a) < (b) ? (a) : (b))

sbuf_t sbuf;

typedef struct cache_entry {
    char uri[MAXLINE];
    char *content;
    int content_length;
    struct cache_entry *next;
} cache_entry_t;

typedef struct {
    cache_entry_t *head;      // 가장 오래된 항목
    cache_entry_t *tail;      // 가장 최근 항목
    int total_size;           // 현재 캐시의 총 크기
    sem_t sem;     // 동기화를 위한 뮤텍스
} cache_t;

cache_t cache;

/* User-Agent header */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* Function prototypes */
int parse_uri(const char *uri, char *hostname, char *port, char *path);
void forward_request(int clientfd);
void handle_response(int serverfd, int clientfd);
void send_error(int clientfd, int status, const char *short_msg, const char *long_msg);

/* Thread routine */
void thread(void* vargp) {
    Pthread_detach(pthread_self());
    while(1) {
        int connfd = sbuf_remove(&sbuf);
        forward_request(connfd);
        Close(connfd);
    }
}

void cache_init() {
    cache.head = NULL;
    cache.tail = NULL;
    cache.total_size = 0;
    if (sem_init(&cache.sem, 0, 1) != 0) { // 세마포어 초기화
        perror("sem_init failed");
        exit(1);
    }
}

int cache_lookup(const char *uri, char **content, int *content_length) {
    if (sem_wait(&cache.sem) < 0) { // 세마포어 대기 (잠금)
        perror("sem_wait failed");
        return 0;
    }

    cache_entry_t *current = cache.head;
    while (current != NULL) {
        if (strcmp(current->uri, uri) == 0) {
            *content = current->content;
            *content_length = current->content_length;
            if (sem_post(&cache.sem) < 0) { // 세마포어 해제 (잠금 해제)
                perror("sem_post failed");
            }
            return 1; // 캐시 히트
        }
        current = current->next;
    }

    if (sem_post(&cache.sem) < 0) { // 세마포어 해제 (잠금 해제)
        perror("sem_post failed");
    }
    return 0; // 캐시 미스
}


void cache_insert(const char *uri, const char *content, int content_length) {
    if (sem_wait(&cache.sem) < 0) { // 세마포어 대기 (잠금)
        perror("sem_wait failed");
        return;
    }

    // 캐시 용량 초과 시 FIFO 방식으로 항목 제거
    while (cache.total_size + content_length > MAX_CACHE_SIZE) {
        if (cache.head == NULL) {
            break; // 캐시가 비어있다면 중단
        }
        cache_entry_t *old = cache.head;
        cache.head = old->next;
        cache.total_size -= old->content_length;
        free(old->content);
        free(old);
    }

    // 새로운 캐시 항목 생성
    cache_entry_t *new_entry = malloc(sizeof(cache_entry_t));
    if (new_entry == NULL) {
        fprintf(stderr, "캐시 항목 메모리 할당 실패\n");
        if (sem_post(&cache.sem) < 0) { // 세마포어 해제
            perror("sem_post failed");
        }
        return;
    }
    strncpy(new_entry->uri, uri, MAXLINE);
    new_entry->content = malloc(content_length);
    if (new_entry->content == NULL) {
        fprintf(stderr, "캐시 콘텐츠 메모리 할당 실패\n");
        free(new_entry);
        if (sem_post(&cache.sem) < 0) { // 세마포어 해제
            perror("sem_post failed");
        }
        return;
    }
    memcpy(new_entry->content, content, content_length);
    new_entry->content_length = content_length;
    new_entry->next = NULL;

    // 캐시에 항목 추가
    if (cache.tail == NULL) {
        cache.head = cache.tail = new_entry;
    } else {
        cache.tail->next = new_entry;
        cache.tail = new_entry;
    }
    cache.total_size += content_length;

    if (sem_post(&cache.sem) < 0) { // 세마포어 해제
        perror("sem_post failed");
    }
}

/* Main function: listens for incoming connections and forwards requests */
int main(int argc, char* argv[]) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* Ignore SIGPIPE to prevent server from terminating when writing to a closed socket */
    // Signal(SIGPIPE, SIG_IGN);

    listenfd = Open_listenfd(argv[1]);
    if (listenfd < 0) {
        perror("Open_listenfd failed");
        exit(1);
    }

    sbuf_init(&sbuf, SBUFSIZE);
    cache_init();

    for(int i=0; i<NTHREADS; i++) { /* Create worker threads */
        Pthread_create(&tid, NULL, thread, NULL);
    }

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd);
    }

    /* Close(listenfd); */
    return 0;
}

/* Function to parse URI and extract hostname, port, and path */
int parse_uri(const char *uri, char *hostname, char *port, char *path) {
    const char *ptr;
    if (strncasecmp(uri, "http://", 7) != 0) {
        return -1;
    }

    ptr = uri + 7; /* Skip "http://" */

    /* Find the end of the hostname */
    const char *slash = strchr(ptr, '/');
    if (slash) {
        size_t host_len = slash - ptr;
        if (host_len >= MAXLINE) return -1;
        strncpy(hostname, ptr, host_len);
        hostname[host_len] = '\0';
        strncpy(path, slash, MAXLINE - 1);
        path[MAXLINE - 1] = '\0';
    } else {
        strncpy(hostname, ptr, MAXLINE - 1);
        hostname[MAXLINE - 1] = '\0';
        strncpy(path, "/", MAXLINE - 1);
        path[MAXLINE - 1] = '\0';
    }

    /* Check if port is specified */
    char *colon = strchr(hostname, ':');
    if (colon) {
        *colon = '\0';
        strncpy(port, colon + 1, MAXLINE - 1);
        port[MAXLINE - 1] = '\0';
    } else {
        strncpy(port, "80", MAXLINE - 1);
        port[MAXLINE - 1] = '\0';
    }

    return 0;
}

/* Function to send an error response to the client */
void send_error(int clientfd, int status, const char *short_msg, const char *long_msg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    snprintf(body, MAXBUF, "<html><title>%d %s</title>", status, short_msg);
    snprintf(body + strlen(body), MAXBUF - strlen(body), "<body bgcolor=\"ffffff\">\r\n");
    snprintf(body + strlen(body), MAXBUF - strlen(body), "%d %s\r\n", status, short_msg);
    snprintf(body + strlen(body), MAXBUF - strlen(body), "<p>%s\r\n", long_msg);
    snprintf(body + strlen(body), MAXBUF - strlen(body), "</body></html>\r\n");

    /* Print the HTTP response */
    snprintf(buf, MAXLINE, "HTTP/1.0 %d %s\r\n", status, short_msg);
    Rio_writen(clientfd, buf, strlen(buf));
    snprintf(buf, MAXLINE, "Content-Type: text/html\r\n");
    Rio_writen(clientfd, buf, strlen(buf));
    snprintf(buf, MAXLINE, "Content-Length: %lu\r\n\r\n", strlen(body));
    Rio_writen(clientfd, buf, strlen(buf));
    Rio_writen(clientfd, body, strlen(body));
}

void forward_request(int clientfd) {
    char buf[MAXLINE];
    char method_buf[MAXLINE], uri[MAXLINE], version_buf[MAXLINE];
    char host[MAXLINE], port_num[MAXLINE], path_buf[MAXLINE];
    rio_t rio_client;
    int serverfd;
    char *cache_content;
    int cache_content_length;

    /* Initialize rio for client */
    Rio_readinitb(&rio_client, clientfd);

    /* Read request line */
    if (!Rio_readlineb(&rio_client, buf, MAXLINE)) {
        fprintf(stderr, "Failed to read request line\n");
        send_error(clientfd, 400, "Bad Request", "Failed to read request line");
        return;
    }

    /* Parse request line */
    if (sscanf(buf, "%s %s %s", method_buf, uri, version_buf) != 3) {
        fprintf(stderr, "Malformed request line\n");
        send_error(clientfd, 400, "Bad Request", "Malformed request line");
        return;
    }

    /* Only handle GET method */
    if (strcasecmp(method_buf, "GET")) {
        fprintf(stderr, "Unsupported method: %s\n", method_buf);
        send_error(clientfd, 501, "Not Implemented", "Proxy does not implement this method");
        return;
    }

    /* 캐시 조회 */
    if (cache_lookup(uri, &cache_content, &cache_content_length)) {
        printf("Cache hit for URI: %s\n", uri);
        Rio_writen(clientfd, cache_content, cache_content_length);
        return;
    }

    /* Parse URI to get hostname, port, and path */
    if (parse_uri(uri, host, port_num, path_buf) < 0) {
        fprintf(stderr, "Failed to parse URI: %s\n", uri);
        send_error(clientfd, 400, "Bad Request", "Failed to parse URI");
        return;
    }

    /* Connect to the target server */
    serverfd = Open_clientfd(host, port_num);
    if (serverfd < 0) {
        fprintf(stderr, "Connection to server failed.\n");
        send_error(clientfd, 502, "Bad Gateway", "Failed to connect to server");
        return;
    }

    /* Initialize rio for server */
    rio_t rio_server;
    Rio_readinitb(&rio_server, serverfd);

    /* Write the request line to the server */
    snprintf(buf, MAXLINE, "%s %s %s\r\n", method_buf, path_buf, version_buf);
    Rio_writen(serverfd, buf, strlen(buf));

    /* Forward headers */
    int host_present = 0;
    while (Rio_readlineb(&rio_client, buf, MAXLINE) > 0) {
        /* End of headers */
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }

        /* Skip headers that need to be replaced */
        if (strncasecmp(buf, "User-Agent:", 11) == 0 ||
            strncasecmp(buf, "Connection:", 11) == 0 ||
            strncasecmp(buf, "Proxy-Connection:", 17) == 0) {
            continue;
        }

        /* Check if Host header is present */
        if (strncasecmp(buf, "Host:", 5) == 0) {
            host_present = 1;
        }

        /* Forward other headers */
        Rio_writen(serverfd, buf, strlen(buf));
    }

    /* Add required headers */
    Rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));
    if (!host_present) {
        char host_hdr[MAXLINE];
        snprintf(host_hdr, MAXLINE, "Host: %s\r\n", host);
        Rio_writen(serverfd, host_hdr, strlen(host_hdr));
    }
    Rio_writen(serverfd, "Connection: close\r\n", 19);
    Rio_writen(serverfd, "Proxy-Connection: close\r\n", 25);
    Rio_writen(serverfd, "\r\n", 2); /* End of headers */

    /* Handle the response from the server and send it back to the client */
    // 응답을 메모리에 저장하여 캐시에 추가
    // 이를 위해 응답을 버퍼에 저장해야 합니다.

    // 임시 버퍼
    char *response_buf = malloc(MAX_CACHE_SIZE);
    if (response_buf == NULL) {
        fprintf(stderr, "메모리 할당 실패\n");
        Close(serverfd);
        return;
    }
    int total_received = 0;
    int n;

    rio_t rio_temp;
    Rio_readinitb(&rio_temp, serverfd);

    /* Read response headers and body */
    while ((n = Rio_readnb(&rio_temp, buf, MAXLINE)) > 0) {
        Rio_writen(clientfd, buf, n);
        if (total_received + n <= MAX_CACHE_SIZE) {
            memcpy(response_buf + total_received, buf, n);
            total_received += n;
        }
    }

    /* 캐시에 저장 */
    cache_insert(uri, response_buf, total_received);
    free(response_buf);

    Close(serverfd);
}


/* Function to handle the server's response and forward it to the client */
void handle_response(int serverfd, int clientfd) {
    rio_t rio;
    char buf[MAXLINE];
    int n;
    int content_length = -1;

    Rio_readinitb(&rio, serverfd);

    /* Forward response headers */
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0) {
        Rio_writen(clientfd, buf, n);

        /* Check for Content-Length header */
        if (strncasecmp(buf, "Content-Length:", 15) == 0) {
            content_length = atoi(buf + 15);
        }

        /* Check for Transfer-Encoding header */
        if (strncasecmp(buf, "Transfer-Encoding:", 18) == 0) {
            // Currently not handling chunked encoding
            // Could set a flag or handle accordingly
        }

        if (strcmp(buf, "\r\n") == 0) {
            break; /* End of headers */
        }
    }

    /* Forward response body */
    if (content_length > 0) {
        int remaining = content_length;
        while (remaining > 0) {
            int to_read = MIN(remaining, MAXLINE);
            n = Rio_readnb(&rio, buf, to_read);
            if (n <= 0) {
                break;
            }
            Rio_writen(clientfd, buf, n);
            remaining -= n;
        }
    } else {
        /* If Content-Length is not present, read until EOF */
        while ((n = Rio_readnb(&rio, buf, MAXLINE)) > 0) {
            Rio_writen(clientfd, buf, n);
        }
    }
}