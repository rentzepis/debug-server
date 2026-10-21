#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_OBJECT_SIZE 102400
#define SBUFSIZE 16
#define NTHREADS 15

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
void doProxy(int connfd);
void parse_url(char *url, char *host, char *uri);
void send_request(rio_t *riop, char *host, int clientfd, char *requestline);
void scan_hdr(char *buf, char *key, char *val);
void send_data(char *url, int clientfd, int connfd);
void *dojob(void *vargp);

/*global variable shared by all threads*/
sbuf_t sbuf;
cache_t cache;

int main(int argc, char **argv)
{
    int i, listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]);

    sbuf_init(&sbuf, SBUFSIZE);
    /*create worker thread to do the task in sbuf*/
    for (i = 0;i < NTHREADS;i++) {
        Pthread_create(&tid, NULL, dojob, NULL);
    }
    init_cache(&cache);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
            Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                        port, MAXLINE, 0);
            printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd);                                                                                       
    }
}

void *dojob(void *vargp)
{
    Pthread_detach(pthread_self());
    while(1){
        int connfd = sbuf_remove(&sbuf);
        doProxy(connfd);
        Close(connfd);
    }
}

/*This function transfers the client/server messages*/
void doProxy(int connfd)
{   
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE], host[MAXLINE], uri[MAXLINE];
    rio_t rio;
    char *colon, requestline[MAXLINE], port[MAXLINE];
    int clientfd, n;
    char cache_val[MAX_OBJECT_SIZE];
    
    // parse the request line
    Rio_readinitb(&rio, connfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) // read the request line
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s\r\n", method, url, version); 
    // GET http://www.cmu.edu/hub/index.html HTTP/1.1
    strcpy(version, "HTTP/1.0");//always forward 1.1
    parse_url(url, host, uri);
    //connect the proxy to host
    colon = index(host, ':');
    if (colon){
        strcpy(port, colon + 1);
        *colon = '\0';
    } else {
        strcpy(port, "80");
    }
    printf("host: %s, port: %s, uri: %s version: %s\r\n", host, port, uri, version);
    printf("url: %s\n", url);
    sprintf(requestline, "%s %s %s", method, uri, version);
    /*check the cache using url as the key*/
    if ((n = get_cache(&cache, url, cache_val)) != 0) {
        printf("cache hit\n");
        Rio_writen(connfd, cache_val, n);
        return;
    }
    printf("cache miss\n");
    clientfd = Open_clientfd(host, port);
    send_request(&rio, host, clientfd, requestline);
    send_data(url, clientfd, connfd);
    Close(clientfd);
}

void parse_url(char *url, char *host, char *uri)
{   
    char *slash;
    char *doubleSlash;
    //host name is between '//' and first '/',http://www.cmu.edu/hub/index.html 
    doubleSlash = strstr(url, "//");  
    if (doubleSlash){
        slash = strstr(doubleSlash + 2, "/");// first '/' after '//'
        // host = doubleSlash + 2;//bug reassign host in local scale
        strcpy(uri, "/");
        if (slash) {
            strcat(uri, slash + 1);
            *slash = '\0';
        }
        strcpy(host, doubleSlash + 2);
    } else { //localhost, e.g. GET / HTTP/1.1
        strcpy(host, "localhost");
        strcpy(uri, url);
    }
    if (uri[strlen(uri)-1] == '/')                   //line:netp:parseuri:slashcheck
	    strcat(uri, "home.html"); 
    strcpy(url, host);
    strcat(url, uri);
}

/*send headers from rio to clientfd*/
void send_request(rio_t *riop, char *host, int clientfd, char *requestline)
{
    char buf[MAXLINE], key[MAXLINE], val[MAXLINE];
    
    int has_host = 0;
    int has_user_agent = 0;
    int has_connection = 0;
    int has_proxy_connection = 0;
    // send the requestline
    Rio_writen(clientfd, requestline, strlen(requestline));
    Rio_readlineb(riop, buf, MAXLINE);
    /*HOST/User-Agent/Connection/Proxy-Connection*/
    while(strcmp(buf, "\r\n")) {       
        //sscanf is error prone 
        //sscanf(buf, "%s %s\r\n", key, val);
        scan_hdr(buf, key, val);
        if (!strcmp(key, "Host")) {
            has_host = 1;
            // retain original one
        } else if (!strcmp(key, "User-Agent")){
            has_user_agent = 1;
            strcpy(val, user_agent_hdr);
        } else if (!strcmp(key, "Connection")){
            has_connection = 1;
            strcpy(val, "close\r\n");
        } else if (!strcmp(key, "Proxy-Connection")) {
            has_proxy_connection = 1;
            strcpy(val, "close\r\n");
        }
        sprintf(buf, "%s: %s", key, val);
        Rio_writen(clientfd, buf, strlen(buf));
        Rio_readlineb(riop, buf, MAXLINE);
    }
    if (!has_host) {
        sprintf(buf, "HOST: %s\r\n", host);
        Rio_writen(clientfd, buf, strlen(buf));
    }
    if (!has_user_agent) {
        sprintf(buf, "User-Agent: %s", user_agent_hdr);
        Rio_writen(clientfd, buf, strlen(buf));
    } 
    if (!has_connection) {
        sprintf(buf, "Connection: %s\r\n", "close");
        Rio_writen(clientfd, buf, strlen(buf));
    } 
    if (!has_proxy_connection) {
        sprintf(buf, "Proxy-Connection: %s\r\n", "close");
        Rio_writen(clientfd, buf, strlen(buf));
    }
    sprintf(buf, "%s", "\r\n");//end of request
    Rio_writen(clientfd, buf, strlen(buf));
    printf("sending all data from client\n");
}

void scan_hdr(char *buf, char *key, char *val)
{
    char *colon;
    colon = index(buf, ':');
    *colon = '\0';
    strcpy(val, colon + 2);
    strcpy(key, buf);
}

/*send the data read from clientfd to the connfd*/
void send_data(char *url, int clientfd, int connfd)
{
    rio_t rio;
    size_t n, size_cache_buf;
    Rio_readinitb(&rio, clientfd);
    char buf[RIO_BUFSIZE];
    char cache_buf[MAX_OBJECT_SIZE];
    char *curr;
    curr = cache_buf;
    // bug: user readline to read jpg or binary?
    // use rio_readnb to read RIO_BUFSIZE
    // cache the content
    while ((n = Rio_readnb(&rio, buf, RIO_BUFSIZE)) != 0)
    {
        printf("proxy server received %d bytes from end point\n", (int)n);
        size_cache_buf += n;
        if (size_cache_buf <= MAX_OBJECT_SIZE){
            memcpy(curr, buf, n);
            curr  += n;
        }
        Rio_writen(connfd, buf, n);
    }
    if (size_cache_buf <= MAX_OBJECT_SIZE) {
        insert_cache(&cache, url, cache_buf, size_cache_buf);
        printf("cache size: %d\n", size_cache_buf);
    }
}