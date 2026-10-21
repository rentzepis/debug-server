#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define SBUFSIZE 16
#define NTHREADS 4
sbuf_t sbuf;        // buffer pool
Cache cache;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

struct URI {
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE]; 
};

void doit(int fd);
void parse_uri(char *uri, struct URI *uri_);
void build_header(char *http_header, struct URI *uri_, rio_t *client_rio);
void *thread(void *vargp);

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    cache_init(&cache);
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(1);
    }

    /* The server accepts requests from the client on the specified port */
    listenfd = Open_listenfd(argv[1]); // argv[1] == port;
    sbuf_init(&sbuf, SBUFSIZE);

    /* Create worker threads */
    for (int i = 0; i < NTHREADS; i++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }
    
    while (1) {
	    clientlen = sizeof(clientaddr);
        /* Accept the first sockaddr client from the request queue for the listenfd. */
	    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);                                         
    }

    return 0;
}

void *thread(void *vargp) {
    Pthread_detach(Pthread_self()); // 分离一个线程，允许其资源在终止时自动回收
    
    while (1) {
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */
        doit(connfd);
        Close(connfd);
    }
}

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int connfd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char server[MAXLINE];
    rio_t client_rio, server_rio;

    /* Read request line and headers */
    Rio_readinitb(&client_rio, connfd);
    if (!Rio_readlineb(&client_rio, buf, MAXLINE))  // size_t == 0 return;
        return;
    
    sscanf(buf, "%s %s %s", method, uri, version);   
    if (strcasecmp(method, "GET")) {               
        printf("Proxy does not implement this method");
        return;
    }    

/* Situation1: The requested content is inside the cache */
    char cache_tag[MAXLINE];
    strcpy(cache_tag, uri);
    int index;
    if ((index = in_cache(&cache, cache_tag)) != -1) {
        Rio_writen(connfd, cache.data[index].obj, strlen(cache.data[index].obj));
        P(&cache.data[index].read);
        cache.data[index].read_cnt--;
        if (cache.data[index].read_cnt == 0)
            V(&cache.data[index].write);
        V(&cache.data[index].read);
        return;
    }

/* Situation2: Not inside the cache */
    /* Parse Uri */
    struct URI *uri_ = (struct URI *)malloc(sizeof(struct URI));
    parse_uri(uri, uri_);
    /* Set the header */
    build_header(server, uri_, &client_rio);

    /* Proxy try to connect server */
    int serverfd = Open_clientfd(uri_->host, uri_->port);
    if (serverfd < 0) {
        printf("Connection failed");
        return;
    }
    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, server, strlen(server));

    char cache_buf[MAX_OBJECT_SIZE];
    int cache_buf_size = 0;
    /* Response to client */
    size_t n;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0)
    {
        cache_buf_size += n;
        if (cache_buf_size < MAX_OBJECT_SIZE) strcat(cache_buf, buf);
        printf("proxy forwarded %d bytes\n", (int)n);
        Rio_writen(connfd, buf, n);
    }
    Close(serverfd);

    if (cache_buf_size < MAX_OBJECT_SIZE) {
        write_cache(&cache, cache_tag, cache_buf);
    }
}
/* $end doit */

void parse_uri(char *uri, struct URI *uri_) {
    // ex: http://www.cmu.edu:8080/hub/index.html
    char *sslash_pos = strstr(uri, "//"); // strstr返回//在uri中的首次出现位置
    if (!sslash_pos) {
        char *path_pos = strstr(uri, "/");
        if (path_pos) {
            strcpy(uri_->path, path_pos);
        }
        strcpy(uri_->port, "80"); // default port
        return;
    }
    else {
        char *colon_pos = strstr(sslash_pos + 2, ":");
        if (colon_pos) {
            int tmp;
            sscanf(colon_pos + 1, "%d%s", &tmp, uri_->path);
            sprintf(uri_->port, "%d", tmp);
            *colon_pos = '\0';
        } else {
            char *path_pos = strstr(sslash_pos + 2, "/");
            if (path_pos) {
                strcpy(uri_->path, path_pos);
                strcpy(uri_->port, "80");
                *path_pos = '\0';
            }
        }
        strcpy(uri_->host, sslash_pos + 2);
    }
    return;
}

void build_header(char *http_header, struct URI *uri_, rio_t *client_rio)
{
    char *User_Agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    char *conn_hdr = "Connection: close\r\n";
    char *prox_hdr = "Proxy-Connection: close\r\n";
    char *host_format = "Host: %s\r\n";
    char *request_header_format = "GET %s HTTP/1.0\r\n";
    char *EOF_hdr = "\r\n";

    char buf[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE], other_hdr[MAXLINE];
    sprintf(request_hdr, request_header_format, uri_->path);

    // read one line
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0)
    {
        if (strcmp(buf, EOF_hdr) == 0) break; /* EOF */

        /* Proxy should use the same Host header as the browser. */
        if (strncasecmp(buf, "Host", strlen("Host")) == 0) // if 相等
        {
            strcpy(host_hdr, buf);
            continue;
        }

        if (strncasecmp(buf, "Connection", strlen("Connection")) && strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) && strncasecmp(buf, "User-Agent", strlen("User-Agent")))
            strcat(other_hdr, buf); // 把buf的内容复制到other_hdr末尾
    }

    /* Web browsers No attach own Host headers */
    if (strlen(host_hdr) == 0) { sprintf(host_hdr, host_format, uri_->host); }
    sprintf(http_header, "%s%s%s%s%s%s%s",
            request_hdr,
            host_hdr,
            conn_hdr,
            prox_hdr,
            User_Agent,
            other_hdr,
            EOF_hdr);
    return;
}