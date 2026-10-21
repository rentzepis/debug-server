/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

#define MAX_REQUEST_SIZE 16384

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);
void doit(int fd, struct sockaddr_storage addr);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int Rio_readnb_w(rio_t *fd, void *ptr, size_t nbytes);
int Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
int Rio_writen_w(int fd, void *usrbuf, size_t n, int line);


sem_t mutex;

struct fd_addr{
    int fd;
    struct sockaddr_storage addr;
};


void *thread(void *vargp){
    // int connfd = *((int *)vargp);
    struct fd_addr* fa = (struct fd_addr*)vargp;
    int fd = fa->fd;
    struct sockaddr_storage addr = fa->addr;
    Pthread_detach(pthread_self());

    doit(fd, addr);
    Close(fd);
    free(vargp);
    return NULL;
}



/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int listenfd, *connfdp;
    struct fd_addr* fa;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    
    pthread_t tid;

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    signal(SIGPIPE, SIG_IGN);

    sem_init(&mutex, 0, 1);


    listenfd = Open_listenfd(argv[1]);

    while(1){
        clientlen = sizeof(struct sockaddr_in);
        // connfd = Malloc(sizeof(int));
        fa = Malloc(sizeof(struct fd_addr));
        fa->fd = Accept(listenfd, (SA *)&(fa->addr), &clientlen);
        Getnameinfo((SA *)&(fa->addr), clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        Pthread_create(&tid, NULL, thread, fa);

        // doit(connfd, clientaddr);
        // Close(connfd);
    }

    Close(listenfd);

    exit(0);
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int fd, struct sockaddr_storage addr){
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], path[MAXLINE], port[MAXLINE];
    rio_t rio_client;

    /* Read request line and headers */
    Rio_readinitb(&rio_client, fd);
    //read request line
    Rio_readlineb_w(&rio_client, buf, MAX_REQUEST_SIZE);
    if(sscanf(buf, "%s %s %s", method, uri, version) < 3){
        printf("error\n"); //fix
        return;
    }

    // if(strcasecmp(method, "GET")){
    //     clienterror(fd, method, "501", "Not Implemented", "Proxy does not implement this method");
    //     return;
    // }

    printf("%s %s %s\n", method, uri, version);

    //get host, path, port from uri
    parse_uri(uri, host, path, port);

    //set proxy as client, connect to server
    int proxyfd = Open_clientfd(host, port);
    if(proxyfd < 0){
        Close(proxyfd);
        printf("Open clientfd failed\n");
        return;
    }

    
    char request[MAX_REQUEST_SIZE];
    //build request line
    sprintf(request, "%s /%s %s\r\n", method, path, version);
    //build request headers
    int content_length = 0;
    while(Rio_readlineb_w(&rio_client, buf, MAX_REQUEST_SIZE) > 0){
        if(strstr(buf, "Content-Length")){
            sscanf(buf + 15, "%d", &content_length);
        }

        sprintf(request, "%s%s", request, buf);
        if(!strcmp(buf, "\r\n")){
            break;
        }
    }

    rio_t rio_server;
    Rio_readinitb(&rio_server, proxyfd);
    //write request line and headers to server
    if(Rio_writen_w(proxyfd, request, strlen(request), 149) == -1){
        printf("Rio_writen_w error\n");
        return;
    }

    //read request body and send to server
    char tmp[3];
    if(strcasecmp(method, "GET")){
        for(int i = 0; i < content_length; ++i){
            Rio_readnb_w(&rio_client, tmp, 1);
            if(Rio_writen_w(proxyfd, tmp, 1, 160) == -1){
                printf("write char error\n"); //fix
                return;
            }
        }
    }
    

    printf("%s", request);

    //send request to server
    //receive infomation from server and send to client
    int n;
    int size = 0;
    content_length = 0;
    //response headers
    while((n = Rio_readlineb_w(&rio_server, buf, MAXLINE)) != 0){
        //content-length line
        if(strstr(buf, "Content-Length")){
            sscanf(buf + 15, "%d", &content_length);
        }
        Rio_writen_w(fd, buf, strlen(buf), 180);
        size += n;

        if(!strcmp(buf, "\r\n")){
            break;
        }
    }
    //response body
    for(int i = 0; i < content_length; ++i){
        n = Rio_readnb_w(&rio_server, buf, 1);
        if(Rio_writen_w(fd, buf, 1, 199) == -1){
            printf("Rio_writen_w error1\n");
            return;
        }
        size += n;
    }

    format_log_entry(buf, (struct sockaddr_in *)&addr, uri, size);
    P(&mutex);
    printf("%s\n", buf);
    V(&mutex);

    Close(proxyfd);
}

void clienterror(int fd, char* cause, char* errnum, char* shortmsg, char* longmsg){
    char buf[MAXLINE], body[MAXBUF];

    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

int Rio_readnb_w(rio_t* rp, void *ptr, size_t nbytes){
    int n;
    if((n = rio_readnb(rp, ptr, nbytes)) < 0){
        printf("Rio_readn error\n");
        return -1;
    }
    return n;
}

int Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen){
    int n;
    if((n = rio_readlineb(rp, usrbuf, maxlen)) < 0){
        printf("Rio_readlineb error\n");
        return -1;
    }
    return n;
}

int Rio_writen_w(int fd, void *usrbuf, size_t n, int line){
    if(rio_writen(fd, usrbuf, n) != n){
        printf("Rio_write error: %d\n", line);
        return -1;
    }
    return n;
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
    char host[INET_ADDRSTRLEN];

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    if (inet_ntop(AF_INET, &sockaddr->sin_addr, host, sizeof(host)) == NULL)
        unix_error("Convert sockaddr_in to string representation failed\n");

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %s %s %zu", time_str, host, uri, size);
}
