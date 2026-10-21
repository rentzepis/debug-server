/*
 * proxy.c - ICS Web proxy
 * 516030910006 Junjie Fang
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

/*
 * struct to store clientaddr and connfd to pass to thread
 */
typedef struct th_argu_t
{
    int tid;
    int connfd;
    struct sockaddr_in clientaddr;
} th_argu;

/* sem for PV */
sem_t mutex_log;

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);
void *doit(void * vargp);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    th_argu *argu;
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }
    /* Ignore SIGPIPE */
    Signal(SIGPIPE, SIG_IGN);
    /* Initialize sem */
    Sem_init(&mutex_log, 0, 1);

    listenfd = Open_listenfd(argv[1]); // then the grade can connect proxy, why?
    clientlen = sizeof(clientaddr);

    pthread_t tid = 0;
    while (1) {
        tid++;
        argu = Malloc(sizeof(argu));

        argu->tid = tid;
        argu->connfd = Accept(listenfd, (SA *)&(argu->clientaddr), &clientlen);

        Pthread_create(&tid, NULL, doit, argu);
    }
    exit(0);
}

/*
 * deal
 * for a request from client is like "GET http://192.168.233.133:36011/ HTTP/1.1" 
 * but why server dont't need "http://"+hostname  
 * when proxy sent request line to server, no need that  
 */
void *doit(void *vargp)
{
    /* Detach this thread */
    Pthread_detach(pthread_self());
    /* get argument from vargp and free it */
    int connfd = ((th_argu*)vargp)->connfd;
    struct sockaddr_in clientaddr = ((th_argu*)vargp)->clientaddr;
    //int tid = ((th_argu*)vargp)->tid;
    Free(vargp);

    /* tmp file for debug */
    //FILE *tmpfd = fopen("tmp.txt", "w");

    char buf[MAXLINE], tmp1[30], tmp2[30];
    char method[MAXLINE], uri[MAXLINE],version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
    char request[MAXLINE], response[MAXLINE];
    char logstring[MAXLINE];

    int n; 
    int response_body_len, request_body_len;
    int remain, tmp;
    ssize_t response_size = 0;
    /*  rio_asServer for passing data with client, rio_asClient for passing data with server */
    rio_t rio_asServer, rio_asClient;
    /* init and get request line */
    Rio_readinitb(&rio_asServer, connfd);
    if (Rio_readlineb_w(&rio_asServer, request, MAXLINE) == 0) {
        Close(connfd);
        return NULL;
    }

    if (sscanf(request, "%s %s %s", method, uri, version) != 3) {
        Close(connfd);
        return NULL;
    }

    if (parse_uri(uri, hostname, pathname, port) < 0) {
        Close(connfd);
        return NULL;
    }
    /* construct request line for sent to server, without host and port*/
    sprintf(buf, "%s /%s %s\r\n", method, pathname, version);
    /* connect to server and init rio and send request line*/
    int clientfd = open_clientfd(hostname, port);
    if (clientfd < 0) {
        Close(connfd);
        return NULL;
    }
    Rio_readinitb(&rio_asClient, clientfd);
    if (Rio_writen_w(clientfd, buf, strlen(buf)) == 0) {
        Close(connfd);
        return NULL;
    }
    /* transpond request header from client to server */
    while((n = Rio_readlineb_w(&rio_asServer, request, MAXLINE)) != 0) {
        
        Rio_writen_w(clientfd, request, n);
        /* until empty line */
        if (strcmp(request, "\r\n") == 0)
            break;
        /* check and get the Content-Length for body use*/
        if (strncasecmp(request, "Content-Length: ", 16) == 0) {
            sscanf(request + 16, "%d", &request_body_len);
        }
        else {
            if (sscanf(request, "%s %s", tmp1, tmp2) != 2) {
                Close(connfd);
                Close(clientfd);
                return NULL;
            }
            if (strlen(tmp2) < 5) {
                Close(connfd);
                Close(clientfd);
                return NULL;
            } 
        }
    }
    if (n == 0){
        Close(connfd);
        Close(clientfd);
        return NULL;
    }

    /* if exist, use readnb to transpond request body from client to server  */
    if (!strcmp(method, "POST")) {
        memset(request,0,strlen(request));
        remain = request_body_len;
        int thisread = request_body_len > MAXLINE ? 1 : request_body_len;
        while (remain > 0){
            tmp = Rio_readnb_w(&rio_asServer, request, thisread);
            if (tmp == thisread){
                if (Rio_writen_w(clientfd, request, thisread) != thisread){
                    Close(connfd);
                    Close(clientfd);
                    return NULL;
                }
            }
            else{
                Close(connfd);
                Close(clientfd);
                return NULL;
            }
            remain -= thisread;
        }
    }
    /* transpond response line and header from server to client */
    while((n = Rio_readlineb_w(&rio_asClient, response, MAXLINE)) != 0) {
        response_size += n;
        Rio_writen_w(connfd, response, n);
        /* check and get the Content-Length for response body use*/
        if (strncasecmp(response, "Content-Length: ", 16) == 0) {
            sscanf(response + 16, "%d", &response_body_len);
            response_size += response_body_len;
        }
        /* until empty line, next is response body */
        if (strcmp(response, "\r\n") == 0)
            break;
    }

    /* use readnb to transpond response body from server to client  */
    remain = response_body_len;
    /* if in 2_1, response_body_len is very large, transpond one by one, or whole */
    int thisread = response_body_len > MAXLINE ? 1 : response_body_len;
    while (remain > 0){
        //int thisread = remain > MAXLINE ? MAXLINE : remain;
        tmp = Rio_readnb_w(&rio_asClient, response, thisread);
        if (tmp != thisread){
            Close(connfd);
            Close(clientfd);
            return NULL;
        }

        if ((tmp = Rio_writen_w(connfd, response, thisread)) != thisread) {
            Close(connfd);
            Close(clientfd);
            return NULL;
        }

        remain -= thisread;
    }
    /* generate log line and write to file */
    format_log_entry(logstring, &clientaddr, uri, response_size );

    P(&mutex_log);
    FILE *logfile = fopen("proxy.log", "a");
    fprintf(logfile, "%s\n", logstring);
    printf("%s\n", logstring);
    fclose(logfile);
    V(&mutex_log);

    /* close debug file */
    //fclose(tmpfd);
    return NULL;
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':') {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
        pathbegin++;
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 12, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %zu", time_str, a, b, c, d, uri, size);
}

/*
 * new wrappers for I/O simply return after printing a warning message when I/O fails
 */
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        //fprintf(fd, "Rio_readlineb_w failed,want %d but %d \n", maxlen, rc);
        return 0;
    }
    return rc;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0) {
        //fprintf(fd, "Rio_readnb_w failed,want %d but %d \n", n, rc);
        return 0;
    }
    return rc;
}

ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n)
{
    ssize_t rc;
    if ((rc = rio_writen(fd, usrbuf, n)) != n) {
        //fprintf(tmpfd, "Rio_writen_w failed,should %d but %d \n", n, rc);
        return 0;
    }

    return rc;
}
