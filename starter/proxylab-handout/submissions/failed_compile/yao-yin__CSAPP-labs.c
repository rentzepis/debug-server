#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

void proxyDoIt(int fd);
void clientError(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *target, int *port, char *filePath);
int build_requestHdrs(rio_t *rp, char *newreq, char *hostname);
void sigchld_handler(int sig);
void *thread(void *vargp);

int main(int argc, char **argv)
{
    int listenfd;
    int *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    // check command line
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    //Signal(SIGCHLD, sigchld_handler);
    signal(SIGPIPE, SIG_IGN);
    init_cache();
    listenfd = Open_listenfd(argv[1]);
    
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Malloc(sizeof(int)); 
        *connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfd);
        //Close(connfd);
    }
    //Close(listenfd);
    free_cache();
    return 0;
}

void proxyDoIt(int fd) 
{
    
    // get GET request from client, send it to the target, save the response.
    // int is_static;
    // struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    //char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rioAsServer, rioAsClient;

    // Read request line and headers
    Rio_readinitb(&rioAsServer, fd);
    if (!Rio_readlineb(&rioAsServer, buf, MAXLINE)) {
        return;
    }
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcmp(method, "GET")) {
        clientError(fd, method, "501", "Not implemented",
        "myProxy does not support this method.");
        return;
    }
    
    if (read_cache(uri, fd) == 1) return;
    // Save all request headers
    // Rio_readn(fd, requestHds, MAXLINE);
    
    // parse 
    char target[MAXLINE], filePath[MAXLINE];
    memset(target, 0, sizeof target);
    memset(filePath, 0, sizeof filePath);
    int port = -1;
    parse_uri(uri, target, &port, filePath);
    
    // Connect to the target and then collect response.
    char port_str[10];
    sprintf(port_str, "%d", port);
    int asClientfd;

    asClientfd = Open_clientfd(target, port_str);
    
    if (asClientfd < 0) {
        printf("connected failed\n");
        return;
    }

    Rio_readinitb(&rioAsClient, asClientfd);
    // Setup first line
    char newRequest[MAXLINE];
    sprintf(newRequest, "GET %s HTTP/1.0\r\n", filePath);
    build_requestHdrs(&rioAsServer, newRequest, target);
    Rio_writen(asClientfd, newRequest, strlen(newRequest));
    
    int n, size = 0;
    char data[MAX_OBJECT_SIZE];
    while ((n = Rio_readlineb(&rioAsClient, buf, MAXLINE))) {
        if (size < MAX_OBJECT_SIZE) {
            memcpy(data+size, buf, n);
            size += n;
        }
        printf("proxy received %d bytes,then send\n",n);
        Rio_writen(fd, buf, n);
    }
    if (size < MAX_OBJECT_SIZE) {
        write_cache(uri, data, size);
    }
    Close(asClientfd);
}

void parse_uri(char *uri, char* target, int* port, char * filePath) {
    *port = 80;
    //uri http://www.cmu.edu/hub/index.html
    char* pos1 = strstr(uri,"//");
    if (pos1 == NULL) {
        pos1 = uri;
    } else pos1 += 2;
    //printf("parse uri pos1 %s\n",pos1);//pos1 www.cmu.edu/hub/index.html

    char* pos2 = strstr(pos1,":");
    /*pos1 www.cmu.edu:8080/hub/index.html, pos2 :8080/hub/index.html */
    if (pos2 != NULL) {
        *pos2 = '\0'; //pos1 www.cmu.edu/08080/hub/index.html
        strncpy(target, pos1, MAXLINE);
        sscanf(pos2+1, "%d%s", port, filePath); //pos2+1 8080/hub/index.html
        *pos2 = ':';
    } else {
        pos2 = strstr(pos1, "/");//pos2 /hub/index.html
        if (pos2 == NULL) {/*pos1 www.cmu.edu*/
            strncpy(target, pos1, MAXLINE);
            strcpy(filePath, "");
            return;
        }
        *pos2 = '\0';
        strncpy(target, pos1, MAXLINE);
        *pos2 = '/';
        strncpy(filePath, pos2, MAXLINE);
    }
}

int build_requestHdrs(rio_t *rp, char *newreq, char *hostname) {
    //already have sprintf(newreq, "GET %s HTTP/1.0\r\n", path);
    char buf[MAXLINE];
    int content_length = 0;
    while(Rio_readlineb(rp, buf, MAXLINE) > 0) {        
	    if (!strcmp(buf, "\r\n")) break;
        if (strstr(buf,"Host:") != NULL) continue;
        if (strstr(buf,"User-Agent:") != NULL) continue;
        if (strstr(buf,"Connection:") != NULL) continue;
        if (strstr(buf,"Proxy-Connection:") != NULL) continue;
        if (!strncasecmp(buf, "Content-length:", 15)) {
            sscanf(buf + 15, "%u", &content_length);
        }
	    sprintf(newreq,"%s%s", newreq, buf);
    }
    sprintf(newreq, "%sHost: %s\r\n", newreq, hostname);
    sprintf(newreq, "%s%s%s%s", newreq, user_agent_hdr,conn_hdr,prox_hdr);
    sprintf(newreq, "%s\r\n", newreq);
    return content_length;
}

void clientError(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

void sigchld_handler(int sig) {
    while (waitpid(-1, 0, WNOHANG) > 0)
        ;
    return;
}

void *thread(void *var) 
{
    int connfd = *((int *)var);
    Pthread_detach(pthread_self());
    Free(var);
    proxyDoIt(connfd);
    //Close(connfd);
    return NULL;
}