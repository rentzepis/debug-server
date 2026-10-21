/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>
#include <stdio.h>
#include <string.h>

typedef struct
{
    struct sockaddr_in sockaddrIn;
    int connfd;
} threadArgs;

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);

void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int read_requesthdrs(rio_t *rp, int client_fd, char *request);

void build_requesthdrs(rio_t *rp, char *newreq, char *hostname, char *port);

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rc = rio_readlineb(rp, usrbuf, maxlen);
    return rc < 0 ? 0 : rc;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc = rio_readnb(rp, usrbuf, n);
    return rc < 0 ? 0 : rc;
}

ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n)
{
    return rio_writen(fd, usrbuf, n) != n ? -1 : 0;
}

/**
 * send request to server
 * @param fd
 */
void doit(int fd, struct sockaddr_in *sockaddrIn)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], path[MAXLINE], request[MAXLINE];
    char port[10];
    int content_length;
    rio_t rio_from_client, rio_to_webServer;

    // Read request line and headers
    Rio_readinitb(&rio_from_client, fd);
    // Rio_writen_w(fd,buf,sizeof (buf));
    if (!Rio_readlineb_w(&rio_from_client, buf, MAXLINE))
    {
        Close(fd);
        return;
    }

    sscanf(buf, "%s %s %s", method, uri, version);

    // Parse URI from GET request
    parse_uri(uri, host, path, port);

    // generate new request
    sprintf(request, "%s /%s %s\r\n", method, path, version);
    // handle header
    content_length = read_requesthdrs(&rio_from_client, fd, request);

    // get fd as client
    int webServer_fd = Open_clientfd(host, port);
    Rio_readinitb(&rio_to_webServer, webServer_fd);

    // parse header
    Rio_writen_w(webServer_fd, request, strlen(request)); // send client header to real server

    //  send request to server
    for (int i = 0; i < content_length; i++)
    {
        if (Rio_readnb_w(&rio_from_client, buf, 1) == 0)  // read and send request body byte by byte
            return -1;
        if (Rio_writen_w(webServer_fd, buf, 1) == -1)
            return -1;
    }

    int n;
    int responseSize = 0;
    while((n = Rio_readlineb_w(&rio_to_webServer, buf, MAXLINE)) != 0){
        responseSize += n;
        // printf("count = %d\n", count);
        if(!strncasecmp(buf, "Content-Length: ", 14)){
            sscanf(buf+15, "%zu", &content_length);
            // printf("responseLength = %d\n",content_length);
        }

        if(Rio_writen_w(fd, buf, strlen(buf)) == -1)
            return 0;
        if(!strncmp(buf, "\r\n", 2))    // response header ends
            break;
    }


    // Response Body
    for(int i=0; i<content_length; i++){
        // printf("read response ing\n");
        if(Rio_readnb_w(&rio_to_webServer, buf, 1) == 0)
            break;
        if(Rio_writen_w(fd, buf, 1) == -1)
            break;
        responseSize++;
    }
    format_log_entry(buf, sockaddrIn, uri, responseSize);
    printf("%s\n", buf);
    Close(webServer_fd);
}

//void build_requesthdrs(rio_t *rp, char *newreq, char *hostname, char *port)
//{
//    // already have sprintf(newreq, "GET %s HTTP/1.0\r\n", path);
//    char buf[MAXLINE];
//
//    while (Rio_readlineb_w(rp, buf, MAXLINE) > 0)
//    {
//        if (!strcmp(buf, "\r\n"))
//            break;
//        if (strstr(buf, "Host:") != NULL)
//            continue;
//        if (strstr(buf, "User-Agent:") != NULL)
//            continue;
//        if (strstr(buf, "Connection:") != NULL)
//            continue;
//        if (strstr(buf, "Proxy-Connection:") != NULL)
//            continue;
//
//        sprintf(newreq, "%s%s", newreq, buf);
//    }
//    sprintf(newreq, "%sHost: %s:%s\r\n", newreq, hostname, port);
//    //    sprintf(newreq, "%s%s%s%s", newreq, user_agent_hdr,conn_hdr,prox_hdr);
//    sprintf(newreq, "%s\r\n", newreq);
//}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

int read_requesthdrs(rio_t *rp, int server_fd, char *request)
{
    char buf[MAXLINE];
    int n;
    int content_length;
    while ((n = Rio_readlineb_w(rp, buf, MAXLINE)) != 0)
    {
        if (!strncasecmp(buf, "Content-Length", 14)){
            // printf("test length\n");
            sscanf(buf + 15, "%zu", &content_length);
        }

        sprintf(request, "%s%s", request, buf);
        if (!strncmp(buf, "\r\n", 2))
            break;
    }
    return content_length;
}

void *thread(void *tArgs)
{
    Pthread_detach(pthread_self());

    int connfd = ((threadArgs *)tArgs)->connfd;
    struct sockaddr_in *addr = &((threadArgs *)tArgs)->sockaddrIn;

    // sent to server
    doit(connfd, addr);
    Free(tArgs);
    Close(connfd);
    return NULL;
}

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    /* Check arguments */

    Signal(SIGPIPE,SIG_IGN);
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    int listenfd;
    int *connfdp;

    socklen_t clientlen;
    /* Enough space for any address */
    char client_hostname[MAXLINE], client_port[MAXLINE];

    struct sockaddr_storage clientaddr;
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        threadArgs * args = Malloc(sizeof(threadArgs));
        clientlen = sizeof(threadArgs);
        int connfd = Accept(listenfd, (SA *)&(args->sockaddrIn), &clientlen);
        args->connfd = connfd;
        pthread_t tid;
        Pthread_create(&tid, NULL, thread, args);
    }

    exit(0);
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

    if (strncasecmp(uri, "http://", 7) != 0)
    {
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
    if (*hostend == ':')
    {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    }
    else
    {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL)
    {
        pathname[0] = '\0';
    }
    else
    {
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
