#include "csapp.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void build_http_header(char *http_header, char *hostname, char *path, char *port, rio_t *client_rio);
void *thread(void *vargp);

int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);
    cache_init();

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    char http_header[MAXLINE];
    rio_t rio;
    int serverfd;
    ssize_t n;
    char *cache_buf = Malloc(MAX_OBJECT_SIZE);
    int obj_size = 0; /* accumulate full object size for caching */
    char url_key[MAXLINE];

    Rio_readinitb(&rio, fd);
    if (rio_readlineb(&rio, buf, MAXLINE) <= 0) {
        Free(cache_buf);
        return;
    }
    /* Reject overlong request lines to avoid buffer misuse */
    if (strlen(buf) >= MAXLINE - 1 && buf[MAXLINE - 2] != '\n') {
        const char *body = "URI too long\n";
        char resp[MAXLINE];
        int resp_len = snprintf(resp, sizeof(resp),
                                "HTTP/1.0 414 Request-URI Too Long\r\n"
                                "Connection: close\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: %zu\r\n\r\n%s",
                                strlen(body), body);
        if (resp_len > 0) {
            rio_writen(fd, resp, resp_len);
        }
        Free(cache_buf);
        return;
    }
    printf("Request:\n");
    printf("%s", buf);
    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        Free(cache_buf);
        return;
    }
    strncpy(url_key, uri, MAXLINE - 1);
    url_key[MAXLINE - 1] = '\0';

    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement the method");
        Free(cache_buf);
        return;
    }

    if (cache_find(url_key, cache_buf, &obj_size)) {
        rio_writen(fd, cache_buf, obj_size);
        printf("Served from cache\n");
        Free(cache_buf);
        return;
    }

    parse_uri(uri, hostname, path, port);

    build_http_header(http_header, hostname, path, port, &rio);

    serverfd = open_clientfd(hostname, port);
    if (serverfd < 0) {
        printf("Connection failed\n");
        Free(cache_buf);
        return;
    }

    if (rio_writen(serverfd, http_header, strlen(http_header)) != strlen(http_header)) {
        Close(serverfd);
        Free(cache_buf);
        return;
    }

    while ((n = read(serverfd, buf, MAXLINE)) != 0) {
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == ECONNRESET) break;
            break;
        }
        
        if (rio_writen(fd, buf, n) != n) {
            if (errno == EPIPE) break;
            break;
        }

        /* accumulate entire object into cache buffer until size limit */
        if (obj_size >= 0 && obj_size + n <= MAX_OBJECT_SIZE) {
            memcpy(cache_buf + obj_size, buf, n);
            obj_size += n;
        } else {
            obj_size = -1; /* mark as too large to cache */
        }
    }

    if (obj_size >= 0) {
        cache_add(url_key, cache_buf, obj_size);
    }

    Close(serverfd);
    Free(cache_buf);
}



void build_http_header(char *http_header, char *hostname, char *path, char *port, rio_t *client_rio)
{
    char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE], host_hdr[MAXLINE];
    
    sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);
    
    other_hdr[0] = '\0';
    host_hdr[0] = '\0';

    while(rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if(strcmp(buf, "\r\n") == 0) break;
        
        if(!strncasecmp(buf, "Host", 4)) {
            strcpy(host_hdr, buf);
            continue;
        }
        
        if(!strncasecmp(buf, "Connection", 10) ||
           !strncasecmp(buf, "Proxy-Connection", 16) ||
           !strncasecmp(buf, "User-Agent", 10)) {
            continue;
        }
        
        strcat(other_hdr, buf);
    }
    
    if(strlen(host_hdr) == 0) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }
    
    sprintf(http_header, "%s%s%s%s%s%s\r\n",
            request_hdr,
            host_hdr,
            "Connection: close\r\n",
            "Proxy-Connection: close\r\n",
            user_agent_hdr,
            other_hdr);
    
    return;
}

void parse_uri(char *uri, char *hostname, char *path, char *port)
{
    char *ptr;
    
    if (strstr(uri, "http://")) {
        ptr = uri + 7;
    } else {
        ptr = uri;
    }
    
    char *port_ptr = strstr(ptr, ":");
    char *path_ptr = strstr(ptr, "/");
    
    if (path_ptr == NULL) {
        strcpy(path, "/");
    } else {
        strcpy(path, path_ptr);
        *path_ptr = '\0';
    }
    
    if (port_ptr != NULL) {
        *port_ptr = '\0';
        strcpy(port, port_ptr + 1);
        strcpy(hostname, ptr);
    } else {
        strcpy(port, "80");
        strcpy(hostname, ptr);
    }
}
