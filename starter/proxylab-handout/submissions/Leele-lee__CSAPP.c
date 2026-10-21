#include <stdio.h>
#include "./csapp.h"
#include "./cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* 
 * parse url static version: http://www.cmu.edu:8080/hub/index.html, http://localhost:8000/
 * or dynamic version: http://bluefish.ics.cs.cmu.edu:8000/cgi-bin/adder?15000&213
 * to get 
 * host: www.cmu.edu
 * path: /hub/index.html
 * port: 8080 (default 80)
 */
int parse_url(char *url, char *host, char *path, char* port) {
    char *pos = strstr(url, "//");
    if (pos == NULL)
        return 0;
    pos += 2;
    char *colon = strstr(pos, ":");
    char *slash = strstr(pos, "/"); 
    printf("colon: %s\n", colon);
    printf("slash: %s\n", slash);

    strcpy(path, slash);
    
    char res[MAXLINE];

    if (colon) {
        strncpy(host, pos, colon - pos);
        host[colon - pos] = '\0'; // bluefish.ics.cs.cmu.edu 8000/cgi-bin/adder?15000&213
        printf("host: %s\n", host);
        strncpy(port, colon + 1, slash - colon - 1); // error! find why?
        port[slash - colon - 1] = '\0';
        printf("port: %s\n", port);
    } else {
        strcpy(port, "80");
        strncpy(host, pos, slash - pos);
        host[slash - pos] = '\0';
    }
    return 1;
}


/*
 * 1. first parse request `GET http://www.cmu.edu/hub/index.html HTTP/1.1` to 
 *    get method, url and version
 * 2. then build new request send to server: `GET /hub/index.html HTTP/1.0`,
 *    ps: pay attention to these request headers Host, User-agent, Connection, Proxy-connection
 * 3. finally read the response from server and forward to client
 */
void handle_request(int connfd) {
    rio_t rio, server_rio;
    int clientfd;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];

    /* Read request line and headers */
    Rio_readinitb(&rio, connfd);
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("Read request line from client: %s\n", buf);

    sscanf(buf, "%s %s %s", method, url, version);
    printf("Before parse_url, we got method: %s, url: %s, version: %s\n", method, url, version);

    // part III, if method is get and can find url in cache, data transfer in query_cache
    if ((!strncmp(method, "GET", 3)) && (query_cache(url, rio) == 1)) {
        printf("Find %s in cache memory\n", url); fflush(stdout);
        return;
    }
    printf("This url: %s didn't in cache memory\n", url); fflush(stdout);

    char host[MAXLINE], path[MAXLINE], port[MAXLINE];
    int parse = parse_url(url, host, path, port);
    if (!parse) {
        fprintf(stderr, "The request's url: %s is not standard\n", url);
        return;
    }
    printf("After parse_url, the url: %s, host: %s, path: %s, port: %s\n", url, host, path, port);

    //open clientfd in proxy in order to pass request to server 
    //now proxy served as client
    clientfd = Open_clientfd(host, port);
    if (clientfd < 0) {
        return;
    }

    /* build and send request line to server */
    sprintf(buf, "%s %s HTTP/1.0\r\n", method, path);
    printf("This is request line send to server: %s\n", buf); fflush(stdout);
    Rio_writen(clientfd, buf, strlen(buf));
    printf("After send request line to server\n"); fflush(stdout);

    /* build request headers and send to server */
    // read requesr headers from client
    while (Rio_readlineb(&rio, buf, MAXLINE) != 0) {
        // when read empty line terminate headers --- "\r\n" break the loop
        if (strcmp(buf, "\r\n") == 0) break;
        
        // if the line start with host:, user-agent:, connection:, proxy-connection:
        // we replace them with our version 
        if ((strncasecmp(buf, "host:", 5) == 0) 
            || (strncasecmp(buf, "user-agent:", 11) == 0) 
            || (strncasecmp(buf, "connection:", 11)) == 0 
            || (strncasecmp(buf, "proxy-connection:", 17) == 0)) {
            continue;
        }
        Rio_writen(clientfd, buf, strlen(buf));
        printf("%s", buf); fflush(stdout);
    }
    printf("After read all headers from client and sent to server\n"); fflush(stdout);

    sprintf(buf, "Host: %s\r\n%s", host, user_agent_hdr); //notice
    Rio_writen(clientfd, buf, strlen(buf));
    printf("%s", buf); fflush(stdout);


    sprintf(buf, "Connection: close\r\n");
    Rio_writen(clientfd, buf, strlen(buf));
    printf("%s", buf); fflush(stdout);


    sprintf(buf, "Proxy-Connection: close\r\n\r\n");
    Rio_writen(clientfd, buf, strlen(buf));
    printf("%s", buf); fflush(stdout);

    printf("clientfd is %ld\n", clientfd); fflush(stdout);

    // accepet response from server and send to client
    Rio_readinitb(&server_rio, clientfd);
    ssize_t n;

    ssize_t total_size = 0;
    int cacheable = 1;
    char cache[MAX_OBJECT_SIZE];

    while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) != 0) {
        Rio_writen(connfd, buf, n);

        if (cacheable) {
            if (total_size + n < MAX_OBJECT_SIZE) {
                memcpy(cache + total_size, buf, n);
                total_size += n;
            } else {
                cacheable = 0;
            }
        }
    }
    if (cacheable) {
        printf("The url: %s is cacheable, total size is: %ld\n", url, total_size); fflush(stdout);
        add_cache(url, cache, total_size);
    }
    Close(clientfd);
}

int main(int argc, char **argv)
{
    // Listening on given port and accept new connect from client 
    // and handle_request in a infinite loop
    int listenfd, *connfdp;
    struct sockaddr_storage clientaddr;
    socklen_t clientlen;
    pthread_t tid;

    Signal(SIGPIPE, SIG_IGN);
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        fprintf(stderr, "Using default port 5050\n");
        listenfd = Open_listenfd("5050");
    } else {
        listenfd = Open_listenfd(argv[1]);
    }

    init_cache();

    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    Close(listenfd);
}

/* Thread routine */
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    printf("Thread %ld handling connfd %d\n", pthread_self(), connfd);

    Pthread_detach(pthread_self());
    Free(vargp);
    handle_request(connfd);

    printf("Thread %ld done with handling connfd %d\n", pthread_self(), connfd);
    Close(connfd);
    return NULL;
}
