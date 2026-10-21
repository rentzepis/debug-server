/* Reference: https://github.com/CrazyIvanPro/ICS/tree/master/Labs/08-Proxylab */

#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* pre-specified headers */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

/* Global variables*/
cache_t cache;

/* Function prototypes */
void *thread(void *vargp);
void proxy(int connfd);
int parse_url(char *url, char *host, char *port, char *path, char *uri);
void get_requesthdrs(char *headers, rio_t *riop, char *host, char *port, char *path);
void clienterror(
    int fd, char *cause, char *errnum,
    char *shortmsg, char *longmsg
);

/* $begin proxymain */
int main(int argc, char **argv) {
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* Ignore SIGPEPE signal */
    Signal(SIGPIPE, SIG_IGN);

    /* Initialize the cache */
    cache_init(&cache);

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo(
            (SA *)&clientaddr, clientlen, hostname, MAXLINE,
            port, MAXLINE, 0
        );
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}
/* $end proxymain */

/* Thread routine */
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    proxy(connfd);
    Close(connfd);
    return NULL;
}

/* 
 * proxy
 *  - forwards the request on to the end server, and sends the response back to client
 */
void proxy(int connfd) {
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], path[MAXLINE], uri[MAXLINE];
    char headers[MAXLINE];
    int clientfd;
    char object_buf[MAX_OBJECT_SIZE];
    size_t n = 0, object_size = 0;
    int cache_line_id;

    /* serverrio: rio between client and proxy, proxy as a server */
    /* clientrio: rio between proxy and server, proxy as a client */
    rio_t serverrio, clientrio;

    /* Read request line and headers */
    Rio_readinitb(&serverrio, connfd);
    if (!Rio_readlineb(&serverrio, buf, MAXLINE))
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, url, version);
    if (strcasecmp(method, "GET")) {
        clienterror(
            connfd, method, "501", "Not Implemented",
            "Proxy does not implement this method"
        );
        return;
    }

    if (parse_url(url, host, port, path, uri)) {
        clienterror(
            connfd, url, "502", "Parse URL Fail",
            "Proxy fails to parse the URL"
        );
        return;
    }

    /* Try to get the response in cache */
    if ((cache_line_id = cache_find(&cache, uri)) != -1) {
        P_reader(&cache, cache_line_id);

        char *content = cache.cache_lines[cache_line_id].content;
        Rio_writen(connfd, content, strlen(content));

        V_reader(&cache, cache_line_id);
        return;
    }

    /* Get request headers */
    get_requesthdrs(headers, &serverrio, host, port, path);

    /* Connect to end server */
    clientfd = Open_clientfd(host, port);
    Rio_readinitb(&clientrio, clientfd);

    /* Forward the request headers to end server */
    Rio_writen(clientfd, headers, strlen(headers));
    
    /* Send the response back to the client */
    while ((n = Rio_readlineb(&clientrio, buf, MAXLINE)) != 0) {
        Rio_writen(connfd, buf, n);
        object_size += n;
        if (object_size < MAX_OBJECT_SIZE)
            strcat(object_buf, buf);
    }

    /* Insert the object into the cache */
    if (object_size < MAX_OBJECT_SIZE)
        cache_insert(&cache, uri, object_buf);

    Close(clientfd);
}

/*
 * parse_url
 *  - parses URL into host, port, path and uri
 *  - return 0 if parse succeeds, -1 if fails
 */
int parse_url(char *url, char *host, char *port, char *path, char *uri) {
    char *host_begin, *port_begin, *path_begin;
    int port_num = 80;

    if (!url || !strlen(url))
        return -1;

    /* Get the begin char of host */
    if ((host_begin = strstr(url, "//")) != NULL)
        host_begin += 2;
    else
        host_begin = url;

    if ((port_begin = strstr(host_begin, ":")) != NULL) {
        /* port sepecified, path might be specified */
        *port_begin = '\0';
        sscanf(host_begin, "%s", host);
        *port_begin = ':';
        port_begin += 1;
        sscanf(port_begin, "%d%s", &port_num, path);
    } else if ((path_begin = strstr(host_begin, "/"))) {
        /* port not specified, but path specified */
        sscanf(path_begin, "%s", path);
        *path_begin = '\0';
        sscanf(host_begin, "%s", host);
        *path_begin = '/';
    } else {
        /* both port and path are not specified */
        sscanf(host_begin, "%s", host);
        path = "";
    }
    sprintf(port, "%d", port_num);

    sprintf(uri, "%s:%s%s", host, port, path);

    return 0;
}

/* 
 * get_requesthdrs
 *  - From rio input, set the request headers and store them in *`headers`
 */
void get_requesthdrs(char *headers, rio_t *riop, char *host, char *port, char *path) {
    char buf[MAXLINE], request_line[MAXLINE], host_hdr[MAXLINE], other_hdrs[MAXLINE];
    int host_flag, ignore_flag;

    /* Request line */
    sprintf(request_line, "GET %s HTTP/1.0\r\n", path);
    
    /* Input headers */
    while (Rio_readlineb(riop, buf, MAXLINE)) {
        host_flag = !strncasecmp(buf, "Host", 4);
        ignore_flag = (
            !strncasecmp(buf, "User-Agent", 10) || 
            !strncasecmp(buf, "Connection", 10) ||
            !strncasecmp(buf, "Proxy-Connection", 16)
        );
        if (!strcmp(buf, "\r\n"))
            break;
        if (host_flag)
            strcpy(buf, host_hdr);
        else if (ignore_flag)
            continue;
        else
            strcat(other_hdrs, buf);
    }

    if (!host_flag)
        sprintf(host_hdr, "Host: %s\r\n", host);
    
    sprintf(
        headers, 
        "%s%s%s%s%s%s\r\n", 
        request_line, 
        host_hdr, 
        user_agent_hdr, 
        connection_hdr, 
        proxy_connection_hdr,
        other_hdrs
    );
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(
    int fd, char *cause, char *errnum,
    char *shortmsg, char *longmsg
) {
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=ffffff>\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Proxy Server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}