#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <signal.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define REQUEST_BUF_SIZE (MAXBUF * 4)
#define CACHE_KEY_SIZE (MAXLINE * 2 + 32)

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
    "Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct cache_entry {
    char *key;
    char *object;
    size_t size;
    unsigned long long last_used;
    int refcnt;
    pthread_rwlock_t rwlock;
    struct cache_entry *next;
} cache_entry_t;

static cache_entry_t *cache_head = NULL;
static size_t cache_total_size = 0;
static unsigned long long cache_clock = 0;
static pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

static void handle_client(int connfd);
static void *thread(void *vargp);
static int parse_uri(const char *uri, char *host, char *port, char *path);
static int build_http_request(rio_t *client_rio,
                              const char *host,
                              const char *path,
                              char *request_buf,
                              size_t request_buf_size);
static void send_error_response(int fd,
                                const char *status,
                                const char *shortmsg,
                                const char *longmsg);
static int should_skip_header(const char *header_line);
static int append_to_buf(char *buf, size_t buf_size, size_t *len, const char *text);
static int build_cache_key(const char *host,
                           const char *port,
                           const char *path,
                           char *key,
                           size_t key_size);
static cache_entry_t *cache_find_locked(const char *key);
static cache_entry_t *cache_acquire(const char *key);
static void cache_release(cache_entry_t *entry);
static int cache_serve(int connfd, const char *key);
static void cache_store(const char *key, const char *object, size_t size);
static cache_entry_t *find_lru_victim_locked(cache_entry_t **victim_prev);
static void free_cache_entry(cache_entry_t *entry);
static void check_pthread_result(int rc, const char *msg);

int main(int argc, char **argv)
{
    int listenfd;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    Signal(SIGPIPE, SIG_IGN);
    listenfd = Open_listenfd(argv[1]);

    while (1) {
        int connfd;
        int *connfdp;
        pthread_t tid;
        socklen_t clientlen;
        struct sockaddr_storage clientaddr;

        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        connfdp = Malloc(sizeof(int));
        *connfdp = connfd;
        Pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}

static void *thread(void *vargp)
{
    int connfd = *((int *)vargp);

    Free(vargp);
    Pthread_detach(Pthread_self());
    handle_client(connfd);
    Close(connfd);
    return NULL;
}

static void handle_client(int connfd)
{
    char buf[MAXLINE];
    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];
    char host[MAXLINE];
    char port[16];
    char path[MAXLINE];
    char cache_key[CACHE_KEY_SIZE];
    char request_buf[REQUEST_BUF_SIZE];
    rio_t client_rio;
    ssize_t n;
    int serverfd;

    Rio_readinitb(&client_rio, connfd);
    n = rio_readlineb(&client_rio, buf, MAXLINE);
    if (n <= 0) {
        return;
    }

    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        send_error_response(connfd, "400 Bad Request", "Bad Request",
                            "Proxy could not parse the request line.");
        return;
    }

    if (strcasecmp(method, "GET") != 0) {
        send_error_response(connfd, "501 Not Implemented", "Not Implemented",
                            "Proxy only supports the GET method.");
        return;
    }

    if (!parse_uri(uri, host, port, path)) {
        send_error_response(connfd, "400 Bad Request", "Bad Request",
                            "Proxy could not parse the request URI.");
        return;
    }

    if (!build_cache_key(host, port, path, cache_key, sizeof(cache_key))) {
        send_error_response(connfd, "400 Bad Request", "Bad Request",
                            "Proxy could not build the cache key.");
        return;
    }

    if (cache_serve(connfd, cache_key)) {
        return;
    }

    if (!build_http_request(&client_rio, host, path, request_buf, sizeof(request_buf))) {
        send_error_response(connfd, "400 Bad Request", "Bad Request",
                            "Proxy could not build the origin request.");
        return;
    }

    serverfd = open_clientfd(host, port);
    if (serverfd < 0) {
        send_error_response(connfd, "502 Bad Gateway", "Bad Gateway",
                            "Proxy failed to connect to the origin server.");
        return;
    }

    if (rio_writen(serverfd, request_buf, strlen(request_buf)) < 0) {
        close(serverfd);
        return;
    }

    {
        rio_t server_rio;
        char object_buf[MAX_OBJECT_SIZE];
        size_t object_size = 0;
        int cacheable = 1;

        Rio_readinitb(&server_rio, serverfd);
        while ((n = rio_readnb(&server_rio, buf, sizeof(buf))) > 0) {
            if (cacheable) {
                if (object_size + (size_t)n <= MAX_OBJECT_SIZE) {
                    memcpy(object_buf + object_size, buf, (size_t)n);
                    object_size += (size_t)n;
                } else {
                    cacheable = 0;
                }
            }

            if (rio_writen(connfd, buf, n) < 0) {
                break;
            }
        }

        if (n == 0 && cacheable) {
            cache_store(cache_key, object_buf, object_size);
        }
    }

    close(serverfd);
}

static int parse_uri(const char *uri, char *host, char *port, char *path)
{
    const char *host_begin;
    const char *path_begin;
    const char *query_begin;
    const char *suffix_begin;
    char host_port[MAXLINE];
    char *port_begin;
    size_t host_len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        return 0;
    }

    host_begin = uri + 7;
    path_begin = strchr(host_begin, '/');
    query_begin = strchr(host_begin, '?');

    if (path_begin == NULL) {
        suffix_begin = query_begin;
    } else if (query_begin == NULL || path_begin < query_begin) {
        suffix_begin = path_begin;
    } else {
        suffix_begin = query_begin;
    }

    if (suffix_begin == NULL) {
        strcpy(path, "/");
        host_len = strlen(host_begin);
    } else if (*suffix_begin == '/') {
        host_len = (size_t)(suffix_begin - host_begin);
        if (strlen(suffix_begin) >= MAXLINE) {
            return 0;
        }
        strcpy(path, suffix_begin);
    } else {
        host_len = (size_t)(suffix_begin - host_begin);
        if (strlen(suffix_begin) + 1 >= MAXLINE) {
            return 0;
        }
        path[0] = '/';
        strcpy(path + 1, suffix_begin);
    }

    if (host_len == 0 || host_len >= sizeof(host_port)) {
        return 0;
    }

    memcpy(host_port, host_begin, host_len);
    host_port[host_len] = '\0';

    port_begin = strchr(host_port, ':');
    if (port_begin != NULL) {
        *port_begin = '\0';
        port_begin++;
        if (*port_begin == '\0' || strlen(port_begin) >= 16) {
            return 0;
        }
        strcpy(port, port_begin);
    } else {
        strcpy(port, "80");
    }

    if (host_port[0] == '\0' || strlen(host_port) >= MAXLINE) {
        return 0;
    }

    strcpy(host, host_port);
    return 1;
}

static int build_http_request(rio_t *client_rio,
                              const char *host,
                              const char *path,
                              char *request_buf,
                              size_t request_buf_size)
{
    char buf[MAXLINE];
    char host_header[MAXLINE];
    char other_headers[REQUEST_BUF_SIZE];
    size_t request_len = 0;
    size_t other_len = 0;
    int has_host_header = 0;
    int written;

    request_buf[0] = '\0';
    other_headers[0] = '\0';
    host_header[0] = '\0';

    written = snprintf(buf, sizeof(buf), "GET %s HTTP/1.0\r\n", path);
    if (written < 0 || (size_t)written >= sizeof(buf)) {
        return 0;
    }
    if (!append_to_buf(request_buf, request_buf_size, &request_len, buf)) {
        return 0;
    }

    while (1) {
        ssize_t n = rio_readlineb(client_rio, buf, sizeof(buf));
        if (n <= 0) {
            return 0;
        }
        if (strcmp(buf, "\r\n") == 0 || strcmp(buf, "\n") == 0) {
            break;
        }

        if (strncasecmp(buf, "Host:", 5) == 0) {
            if (strlen(buf) >= sizeof(host_header)) {
                return 0;
            }
            strcpy(host_header, buf);
            has_host_header = 1;
            continue;
        }

        if (should_skip_header(buf)) {
            continue;
        }

        if (!append_to_buf(other_headers, sizeof(other_headers), &other_len, buf)) {
            return 0;
        }
    }

    if (!has_host_header) {
        written = snprintf(host_header, sizeof(host_header), "Host: %s\r\n", host);
        if (written < 0 || (size_t)written >= sizeof(host_header)) {
            return 0;
        }
    }

    if (!append_to_buf(request_buf, request_buf_size, &request_len, host_header) ||
        !append_to_buf(request_buf, request_buf_size, &request_len, user_agent_hdr) ||
        !append_to_buf(request_buf, request_buf_size, &request_len, "Connection: close\r\n") ||
        !append_to_buf(request_buf, request_buf_size, &request_len, "Proxy-Connection: close\r\n") ||
        !append_to_buf(request_buf, request_buf_size, &request_len, other_headers) ||
        !append_to_buf(request_buf, request_buf_size, &request_len, "\r\n")) {
        return 0;
    }

    return 1;
}

static void send_error_response(int fd,
                                const char *status,
                                const char *shortmsg,
                                const char *longmsg)
{
    char body[MAXBUF];
    char header[MAXBUF];
    int body_len;
    int header_len;

    body_len = snprintf(body, sizeof(body),
                        "<html><title>Proxy Error</title>"
                        "<body bgcolor=\"ffffff\">\r\n"
                        "%s: %s\r\n"
                        "<p>%s\r\n"
                        "<hr><em>CS:APP Proxy</em>\r\n",
                        status, shortmsg, longmsg);
    if (body_len < 0) {
        return;
    }
    if ((size_t)body_len >= sizeof(body)) {
        body_len = (int)sizeof(body) - 1;
    }

    header_len = snprintf(header, sizeof(header),
                          "HTTP/1.0 %s\r\n"
                          "Content-type: text/html\r\n"
                          "Content-length: %d\r\n"
                          "Connection: close\r\n"
                          "\r\n",
                          status, body_len);
    if (header_len < 0) {
        return;
    }

    rio_writen(fd, header, (size_t)header_len);
    rio_writen(fd, body, (size_t)body_len);
}

static int should_skip_header(const char *header_line)
{
    return strncasecmp(header_line, "Connection:", 11) == 0 ||
           strncasecmp(header_line, "Proxy-Connection:", 17) == 0 ||
           strncasecmp(header_line, "User-Agent:", 11) == 0;
}

static int append_to_buf(char *buf, size_t buf_size, size_t *len, const char *text)
{
    size_t text_len = strlen(text);

    if (*len + text_len >= buf_size) {
        return 0;
    }

    memcpy(buf + *len, text, text_len);
    *len += text_len;
    buf[*len] = '\0';
    return 1;
}

static int build_cache_key(const char *host,
                           const char *port,
                           const char *path,
                           char *key,
                           size_t key_size)
{
    int written = snprintf(key, key_size, "%s:%s%s", host, port, path);

    return written >= 0 && (size_t)written < key_size;
}

static cache_entry_t *cache_find_locked(const char *key)
{
    cache_entry_t *entry;

    for (entry = cache_head; entry != NULL; entry = entry->next) {
        if (strcmp(entry->key, key) == 0) {
            return entry;
        }
    }

    return NULL;
}

static cache_entry_t *cache_acquire(const char *key)
{
    cache_entry_t *entry;

    pthread_mutex_lock(&cache_mutex);
    entry = cache_find_locked(key);
    if (entry != NULL) {
        entry->refcnt++;
        entry->last_used = ++cache_clock;
    }
    pthread_mutex_unlock(&cache_mutex);

    return entry;
}

static void cache_release(cache_entry_t *entry)
{
    pthread_mutex_lock(&cache_mutex);
    entry->refcnt--;
    pthread_mutex_unlock(&cache_mutex);
}

static int cache_serve(int connfd, const char *key)
{
    cache_entry_t *entry = cache_acquire(key);

    if (entry == NULL) {
        return 0;
    }

    check_pthread_result(pthread_rwlock_rdlock(&entry->rwlock),
                         "pthread_rwlock_rdlock");
    rio_writen(connfd, entry->object, entry->size);
    check_pthread_result(pthread_rwlock_unlock(&entry->rwlock),
                         "pthread_rwlock_unlock");
    cache_release(entry);
    return 1;
}

static void cache_store(const char *key, const char *object, size_t size)
{
    cache_entry_t *entry;
    cache_entry_t *victim;
    cache_entry_t *victim_prev;

    if (size > MAX_OBJECT_SIZE) {
        return;
    }

    entry = Malloc(sizeof(cache_entry_t));
    entry->key = Malloc(strlen(key) + 1);
    entry->object = Malloc(size > 0 ? size : 1);
    memcpy(entry->key, key, strlen(key) + 1);
    memcpy(entry->object, object, size);
    entry->size = size;
    entry->refcnt = 0;
    entry->next = NULL;
    check_pthread_result(pthread_rwlock_init(&entry->rwlock, NULL),
                         "pthread_rwlock_init");

    pthread_mutex_lock(&cache_mutex);

    if (cache_find_locked(key) != NULL) {
        pthread_mutex_unlock(&cache_mutex);
        free_cache_entry(entry);
        return;
    }

    while (cache_total_size + size > MAX_CACHE_SIZE) {
        victim = find_lru_victim_locked(&victim_prev);
        if (victim == NULL) {
            break;
        }

        if (victim_prev == NULL) {
            cache_head = victim->next;
        } else {
            victim_prev->next = victim->next;
        }
        cache_total_size -= victim->size;
        free_cache_entry(victim);
    }

    if (cache_total_size + size > MAX_CACHE_SIZE) {
        pthread_mutex_unlock(&cache_mutex);
        free_cache_entry(entry);
        return;
    }

    entry->last_used = ++cache_clock;
    entry->next = cache_head;
    cache_head = entry;
    cache_total_size += size;
    pthread_mutex_unlock(&cache_mutex);
}

static cache_entry_t *find_lru_victim_locked(cache_entry_t **victim_prev)
{
    cache_entry_t *entry;
    cache_entry_t *prev = NULL;
    cache_entry_t *lru = NULL;
    cache_entry_t *lru_prev = NULL;

    for (entry = cache_head; entry != NULL; entry = entry->next) {
        if (entry->refcnt == 0 &&
            (lru == NULL || entry->last_used < lru->last_used)) {
            lru = entry;
            lru_prev = prev;
        }
        prev = entry;
    }

    *victim_prev = lru_prev;
    return lru;
}

static void free_cache_entry(cache_entry_t *entry)
{
    check_pthread_result(pthread_rwlock_destroy(&entry->rwlock),
                         "pthread_rwlock_destroy");
    Free(entry->object);
    Free(entry->key);
    Free(entry);
}

static void check_pthread_result(int rc, const char *msg)
{
    if (rc != 0) {
        posix_error(rc, (char *)msg);
    }
}
