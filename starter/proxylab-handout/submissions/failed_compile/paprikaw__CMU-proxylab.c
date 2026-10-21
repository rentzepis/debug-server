#include <stdio.h>
#include "csapp.h"
#include "url_parser.h"
#include "sbuf.h"
#include "cache.h"
// Sequential -> cocurrent ->
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16
#define CACHESIZE 16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
int parse_request(int contfd, char *request_buf, char *host, char *port, char *url);
int parse_request_line(rio_t *client_rp, char *parsed_request,
                       char *host, char *port, char *uri, char *url);
void *thread(void *vargp);

sbuf_t sbuf;          /* shared buffer of connected descriptors */
cache_t shared_cache; /* shared cache for all worker */

/* globol variable for reader-writer modal */
int readcnt;    /* Initially = 0 */
sem_t mutex, w; /* Both initially = 1 */

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);
    init_cache(CACHESIZE, &shared_cache);
    /* Initialize semaphore*/
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);

    pthread_t tid;

    for (int i = 0; i < NTHREADS; i++) /* Create worker threads */ // line:conc:pre:begincreate
        Pthread_create(&tid, NULL, thread, NULL);                  // line:conc:pre:endcreate
    while (1)
    {
        struct sockaddr_in clientaddr;
        clientlen = sizeof(clientaddr);
        // 接收client发来的链接
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */
        printf("connected to %d\n", clientaddr.sin_addr.s_addr);
    }
    exit(0);
}

/*
 * thread - single thread routine, handle connections
 * return -1 if message format is not correct or exceed maximum object size limit
 */
void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    char request_buf[MAX_OBJECT_SIZE];
    char response[MAX_OBJECT_SIZE];
    char host[MAXLINE];
    char port[MAXLINE];
    char url[MAXLINE];
    size_t receive_size;
    size_t cache_res_buf_size;
    int proxy_client_fd;
    int connfd;
    cache_node_t *cache_res = NULL;

    while (1)
    {
        connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */ // line:conc:pre:removeconnfd

        // 处理client发来的链接, 将处理好的内容放在request_buf中
        if (parse_request(connfd, request_buf, host, port, url) == -1)
        {
            Close(connfd);
            continue;
        }

        printf("received msg from client:\n");
        printf(request_buf);

        /* reader setup semaphore*/
        P(&mutex);
        readcnt++;
        if (readcnt == 1)
        {
            P(&w);
        }
        V(&mutex);

        cache_res = lookup_cache(&shared_cache, url);

        if (cache_res != NULL)
        {
            memcpy(request_buf, cache_res->buf, cache_res->buf_size);
            cache_res_buf_size = cache_res->buf_size;
            /* reader finish semaphore*/
            P(&mutex);
            readcnt--;
            if (readcnt == 0)
            {
                V(&w);
            }
            V(&mutex);

            //将服务器内容发送给client
            Rio_writen(connfd, request_buf, cache_res_buf_size);

            // 将处理好的内容转发给目标host
            Close(connfd);
            continue;
        }
        /* reader finish semaphore*/
        P(&mutex);
        readcnt--;
        if (readcnt == 0)
        {
            V(&w);
        }
        V(&mutex);

        // 向对向服务器发送信息
        if ((proxy_client_fd = Open_clientfd(host, port)) < 0)
        {
            continue;
        };

        Rio_writen(proxy_client_fd, request_buf, strlen(request_buf));
        receive_size = Rio_readn(proxy_client_fd, request_buf, MAX_OBJECT_SIZE);
        close(proxy_client_fd);

        /* 将收到的message存到cache中 */
        P(&w);
        insert_cache(&shared_cache, request_buf, receive_size);
        V(&w);

        printf("received msg from server:\n");
        Rio_writen(STDOUT_FILENO, request_buf, receive_size);

        //将服务器内容发送给client
        Rio_writen(connfd, request_buf, receive_size);

        // 将处理好的内容转发给目标host
        Close(connfd);
        printf("connection is closed\n");
    }
}
/*
 * parse_request - parse the whole request
 * return -1 if message format is not correct or exceed maximum object size limit
 */
int parse_request(int contfd, char *request_buf, char *host, char *port, char *url)
{
    char buf[MAXLINE];
    char parsed_request_line[MAXLINE];
    char header_field[MAXLINE];
    char header_field_value[MAXLINE];
    char abs_path[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, contfd);

    /* parse request line */
    if (parse_request_line(&rio, parsed_request_line, host, port, abs_path, url) == -1)
    {
        return -1;
    }
    printf("request line:\n");
    snprintf(request_buf, MAX_OBJECT_SIZE, "%s", parsed_request_line);

    int host_hdr_exist = 0, conn_hdr_exist = 0, proxy_conn_hdr_exist = 0;
    while (1)
    {
        Rio_readlineb(&rio, buf, MAXLINE);
        if (!strcmp(buf, "\r\n"))
        {
            break;
        }
        if (strstr(buf, "Host:"))
        {
            host_hdr_exist = 1;
            strcat(request_buf, buf);
        }
        else if (strstr(buf, "Connection:"))
        {
            conn_hdr_exist = 1;
            strcat(request_buf, conn_hdr);
        }
        else if (strstr(buf, "Proxy connection:"))
        {
            proxy_conn_hdr_exist = 1;
            strcat(request_buf, proxy_conn_hdr);
        }
        else
        {
            strcat(request_buf, buf);
        }
    }
    strcat(request_buf, user_agent_hdr);
    if (!host_hdr_exist)
    {
        char host_hdr[MAXLINE];
        sprintf(host_hdr, "Host: %s\r\n", host);
        strcat(request_buf, host_hdr);
    }
    if (!conn_hdr_exist)
    {
        strcat(request_buf, conn_hdr);
    }
    if (!proxy_conn_hdr_exist)
    {
        strcat(request_buf, proxy_conn_hdr);
    }
    strcat(request_buf, "\r\n");
    return 0;
}

/*
 * parse_request_line - taking a request line and parse properly
 * fd is connection file descriptor used to transform error handling page
 */
int parse_request_line(rio_t *client_rp, char *parsed_request,
                       char *host, char *port, char *uri, char *url)
{
    char request_line[MAXLINE];
    char method[MAXLINE], version[MAXLINE];

    if (!Rio_readlineb(client_rp, request_line, MAXLINE))
        return -1;

    sscanf(request_line, "%s %s %s", method, url, version);

    // Check request method
    if (strcasecmp(method, "GET"))
    {
        clienterror(client_rp->rio_fd, method, "501", "Not Implemented",
                    "The proxy does not implement this method");
        return -1;
    }

    URL_INFO info;
    split_url(&info, url);
    strcpy(host, info.host);
    strcpy(port, info.port);
    strcpy(uri, info.path);

    // Check HTTP version
    if (strcmp("HTTP/1.0", version) && strcmp("HTTP/1.1", version))
    {
        clienterror(client_rp->rio_fd, method, "400", "Bad request",
                    "Invalid HTTP version");
        return -1;
    }
    else
    {
        strcpy(version, "HTTP/1.0");
    }

    sprintf(parsed_request, "%s %s %s\r\n", method, uri, version);
    return 0;
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "ffffff"
                  ">\r\n",
            body);
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