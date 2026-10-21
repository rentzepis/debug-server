#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

pthread_rwlock_t lock;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";


/* Functions */
void doit(int connfd);
void *thread(void *vargp);
int parse_uri(char *uri, char *hostname, char *path, int *port);
void close_wrapper(int fd);


int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;
    

    if(argc != 2){
        fprintf(stderr,"Usage :%s <port> \n", argv[0]);
        exit(1);
    }

    // ignore sigpipes
    signal(SIGPIPE, SIG_IGN);

    // initialize reader/writer lock for cache
    pthread_rwlock_init(&lock, NULL);

    // establish client port (default: 29094)
    if (!argv[1]){
        printf("Missing command line port number\n");
        return -1;
    }

    listenfd = Open_listenfd(argv[1]);
    if (listenfd < 0)
        printf("open_listenfd failed.\n");
    else{
        while(1){
            clientlen = sizeof(clientaddr);
            connfdp = Malloc(sizeof(int));
            *connfdp = Accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
            if (connfdp < 0){
                printf("Accept failed.\n");
                Free(connfdp);
            }
            else
                Pthread_create(&tid, NULL, thread, connfdp);
        }
    }
    close_wrapper(listenfd);
    pthread_rwlock_destroy(&lock);
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


void doit(int connfd)
{
    int n, serverfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char http_hdr[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE], portstr[MAXLINE];

    int port = 80;
    rio_t rio, server_rio;

    Rio_readinitb(&rio,connfd);
    Rio_readlineb(&rio,buf,MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if(strcasecmp(method, "GET")){
        printf("Proxy does not implement this method");
        return;
    }
 
    parse_uri(uri, hostname, path, &port);
    sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);
    sprintf(host_hdr, "Host: %s\r\n", hostname);
    sprintf(http_hdr, "%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr, prox_hdr, user_agent_hdr, "\r\n");

    sprintf(portstr, "%d", port);
    serverfd = Open_clientfd(hostname, portstr);

    Rio_readinitb(&server_rio,serverfd);
    Rio_writen(serverfd, http_hdr, strlen(http_hdr));
    while((n = Rio_readlineb(&server_rio, buf, MAXLINE))!=0)
        Rio_writen(connfd,buf,n);
    Close(serverfd);
}


int parse_uri(char *uri, char *hostname, char *path, int *port)
{
    char *host_start = uri + 7;
    char *port_start, *path_start;

    if (strncmp(uri, "http://", 7) != 0)
        host_start = uri;

    port_start = strstr(host_start, ":");
    if (port_start != NULL){
        *port_start = '\0';
        sscanf(host_start, "%s", hostname);
        sscanf(port_start+1, "%d%s", port, path);
    }
    else{
        path_start = strstr(host_start, "/");
        if (path_start != NULL){
            path = path_start;
            *path_start = '\0';
            hostname = host_start;
        }
        else{
            hostname = host_start;
            *path = '\0';
        }
    }
    return 0;
}


void close_wrapper(int fd) {
    if (close(fd) < 0)
        printf("Error closing file.\n");
}

