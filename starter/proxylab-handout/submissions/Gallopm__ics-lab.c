/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>
#include <unistd.h>

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

typedef struct {
    struct sockaddr_storage clientaddr;
    int connfd;
} thread_args_t;

sem_t sem;

void unix_error_w(char *msg)
{
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
}

int Open_clientfd_w(char *hostname, char *port) 
{
    int rc;

    if ((rc = open_clientfd(hostname, port)) < 0) 
	    unix_error_w("Open_clientfd error");
    return rc;
}

ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) 
{
    ssize_t n;
  
    if ((n = rio_readn(fd, ptr, nbytes)) < 0)
	    unix_error_w("Rio_readn error");
    return n;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
	    unix_error_w("Rio_readlineb error");
        return -1;
    }
    return rc;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0) {
	    unix_error_w("Rio_readnb error");
        return -1;
    }
    return rc;
}

void Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) != n) {
	    //unix_error_w("Rio_writen error");
    }
}

void doit(int connfd, struct sockaddr_in* clientaddr)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char request[MAXLINE * 4];
    rio_t rio_client, rio_server;
    Rio_readinitb(&rio_client, connfd);
    Rio_readlineb_w(&rio_client, buf, MAXLINE);
    if(sscanf(buf, "%s %s %s", method, uri, version) != 3)  return;
    if(strcmp(version, "HTTP/1.1"))     return;

    char host[MAXLINE], port[MAXLINE], path[MAXLINE];
    if(parse_uri(uri, host, path, port) < 0)    return;

    if(!strcasecmp(method, "GET")) {
        sprintf(request, "%s /%s %s\r\n", method, path, version);

        ssize_t n = 0;
        while((n = Rio_readlineb_w(&rio_client, buf, MAXLINE)) > 0) {
            strcat(request, buf);
            if(!strcmp(buf, "\r\n"))    break;
            if(strstr(buf, ": ") == NULL)    return;
        }

        if(n <= 0)   return;

        int fd = Open_clientfd_w(host, port);
        Rio_readinitb(&rio_server, fd);
        Rio_writen_w(fd, request, strlen(request));

        ssize_t length = 0, sum = 0;

        while((n = Rio_readlineb_w(&rio_server, buf, MAXLINE)) > 0) {
            sum += n;
            Rio_writen_w(connfd, buf, n);
            if(strstr(buf, "Content-Length") != NULL)
                length = atoi(buf + 16);
            if(!strcmp(buf, "\r\n"))    break;
        }

        if(n <= 0)   { Close(fd); return; }

        if(length) {
            sum += length;
            while(length > 0) {
                if(Rio_readnb_w(&rio_server, buf, 1) > 0) {
                    Rio_writen_w(connfd, buf, 1);
                    length -= 1;
                };
            }
        }

        char logstring[MAXLINE];
        format_log_entry(logstring, clientaddr, uri, sum);
        P(&sem);
        printf("%s\n", logstring);
        V(&sem);

        Close(fd);
    } else if(!strcasecmp(method, "POST")) {
        sprintf(request, "%s /%s %s\r\n", method, path, version);

        ssize_t length = 0, n = 0;
        while((n = Rio_readlineb_w(&rio_client, buf, MAXLINE)) > 0) {
            strcat(request, buf);
            if(strstr(buf, "Content-Length") != NULL)
                length = atoi(buf + 16);
            if(!strcmp(buf, "\r\n"))    break;
            if(strstr(buf, ": ") == NULL)    return;
        }

        if(n <= 0)   return;

        int fd = Open_clientfd_w(host, port);
        if(fd < 0)  return;
        Rio_readinitb(&rio_server, fd);
        Rio_writen_w(fd, request, strlen(request));

        sleep(2);
        if(length) {
            char* tmp = Malloc(length);
            if(Rio_readnb_w(&rio_client, tmp, length) < length) {
                Close(fd);
                return;
            }  
            Rio_writen_w(fd, tmp, length);
            Free(tmp);
        }

        ssize_t sum = 0;
        length = 0;
        while((n = Rio_readlineb_w(&rio_server, buf, MAXLINE)) > 0) {
            sum += n;
            Rio_writen_w(connfd, buf, n);
            if(strstr(buf, "Content-Length") != NULL)
                length = atoi(buf + 16);
            if(!strcmp(buf, "\r\n"))    break;
        }

        if(n <= 0)   { Close(fd); return; }

        sleep(2);
        if(length) {
            sum += length;
            while(length > MAXLINE) {
                usleep(10000);
                if(Rio_readnb_w(&rio_server, buf, MAXLINE) < MAXLINE) {
                    Close(fd);
                    return;
                }
                Rio_writen_w(connfd, buf, MAXLINE);
                length -= MAXLINE;
            }
            usleep(100000);
            if(Rio_readnb_w(&rio_server, buf, length) < length) {
                Close(fd);
                return;
            }  
            Rio_writen_w(connfd, buf, length);
        }

        char logstring[MAXLINE];
        format_log_entry(logstring, clientaddr, uri, sum);
        P(&sem);
        printf("%s\n", logstring);
        V(&sem);

        Close(fd);
    }
}

void* thread(void* argsPtr)
{
    thread_args_t args = *((thread_args_t*)argsPtr);
    Pthread_detach(Pthread_self());
    Free(argsPtr);

    doit(args.connfd, (struct sockaddr_in*)&(args.clientaddr));
    Close(args.connfd);
    return NULL;
}


/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    int listenfd;
    pthread_t tid;
    socklen_t clientlen;
    char host[MAXLINE], port[MAXLINE];
    thread_args_t* args;

    listenfd = Open_listenfd(argv[1]);
    signal(SIGPIPE, SIG_IGN);
    Sem_init(&sem, 0, 1);
    while(1) {
        clientlen = sizeof(struct sockaddr_storage);
        args = (thread_args_t*)Malloc(sizeof(thread_args_t));
        args->connfd = Accept(listenfd, (SA*)&(args->clientaddr), &clientlen);
        Getnameinfo((SA*)&(args->clientaddr), clientlen, host, MAXLINE, port, MAXLINE, 0);

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
