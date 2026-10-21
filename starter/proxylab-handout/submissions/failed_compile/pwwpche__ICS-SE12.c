/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Andrew Carnegie, ac00@cs.cmu.edu
 *     Harry Q. Bovik, bovik@cs.cmu.edu
 *
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */

#include "csapp.h"
#include<signal.h>
#define DEBUG_PIC1
/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry( struct sockaddr_in *sockaddr,char *uri, int size);
int process(int connfd, int tid,struct sockaddr_in);
void* thread(void* arg);
void clienterror(int fd, char *errnum, char *shortmsg);
int Open_clientfd_r(char* hostname,int port_web);


ssize_t Rio_readn_r(int fd, void *ptr, size_t nbytes);
ssize_t Rio_writen_r(int fd, void *usrbuf, size_t n);
void Rio_readinitb_r(rio_t *rp, int fd);
ssize_t Rio_readnb_r(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb_r(rio_t *rp, void *usrbuf, size_t maxlen);

/*
 * main - Main routine for the proxy program
 */

 sem_t lock_gethost;
 sem_t lock_log;
 sem_t lock_client;

FILE *f_log;

int main(int argc, char **argv)
{

    /* Check arguments */
    signal(SIGPIPE,SIG_IGN);
    if (argc != 2) {
	fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	exit(0);
    }

    Sem_init(&lock_gethost,0,1);
    Sem_init(&lock_log,0,1);

    int listenfd, port, clientlen;
    int *connfd;
    struct sockaddr_in clientaddr;
    pthread_t tid;

    port = atoi(argv[1]);
	listenfd = Open_listenfd(port);
	if(listenfd == 0){
		fprintf(stderr, "Listenfd failed at port %s\n", argv[0]);
	}
    while(1){
		clientlen = sizeof(clientaddr);
		connfd = Malloc(sizeof(int) + sizeof(struct sockaddr_in) + 8);
		*connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		/* determine the domain name and IP address of the client */
        *(struct sockaddr_in*)(connfd + 1) = clientaddr;
        Pthread_create(&tid, NULL, thread, (void*)(connfd));
    }
    fclose(f_log);
    return 0;
}

void* thread(void* arg){
/*
    printf("In thread, tid is %d\n",Pthread_self());
*/
    int connfd = *(int*)arg;
    struct sockaddr_in clientaddr = *(struct sockaddr_in*)((int*)arg + 1);
    fflush(stdout);
    Pthread_detach(pthread_self());
    process(connfd,pthread_self(), clientaddr);
    Free(arg);

/*
    printf("Done thread, tid is %d\n",Pthread_self());
*/
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
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
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
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')
	*port = atoi(hostend + 1);

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
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry( struct sockaddr_in *sockaddr,
		      char *uri, int size)
{
    P(&lock_log);

    f_log = fopen("log.txt","a+");
    char *logstring = malloc(sizeof(char)*MAXLINE);
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
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s", time_str, a, b, c, d, uri);
    fprintf(f_log, "%s %d\n",logstring, size);
    fflush(f_log);
    free(logstring);
    fclose(f_log);
    V(&lock_log);

}

int process(int connfd,int tid,struct sockaddr_in clientaddr){
    char buf[MAXLINE], buf2[MAXLINE];
    char hostname[MAXLINE],pathname[MAXLINE],uri[MAXLINE],uri2[MAXLINE];
    char method[MAXLINE], version[MAXLINE];
    rio_t rio_web;
    rio_t rio_client;
    int fd_web, port_web, length,retValue = 0;
    int currentLength = 0;
    Rio_readinitb(&rio_client, connfd);

    /*Process client request (web uri)*/
    length = Rio_readlineb(&rio_client, buf, MAXLINE);
    if(length == 0)
        return -2;
    strcpy(buf2,buf);

    int fd_i = 0;
    /*Extract uri*/
    sscanf(buf2, "%s %s %s",method, uri, version);
    strcpy(uri2,uri);
    parse_uri(uri, hostname, pathname, &port_web);
    for(fd_i = 0 ; fd_i < 3 ; fd_i++){
        fd_web = Open_clientfd_r(hostname, port_web);
        if(fd_web != -1)
            break;
    }
    if(fd_web == -1){
        printf("DNS ERROR\n");
        close(connfd);
        return -1;
    }
    Rio_readinitb(&rio_web, fd_web);
    Rio_writen_r(fd_web, buf, length);


    /*Parse rest of the request header*/
#ifdef DEBUG_PIC
    printf("tid_%d, Request Header\n");
#endif
    while( (length = Rio_readlineb(&rio_client,buf, MAXLINE)) ){
#ifdef DEBUG_PIC3
        printf("tid_%d, %s\n",tid, buf);
#endif
       	if ((strstr(buf, "Proxy-Connection:") == NULL) ){
            Rio_writen_r(fd_web, buf, length);
        }else{
            char* temp = "Connection: close\r\n";
            Rio_writen_r(fd_web, temp, strlen(temp));
        }
        /*Request Header Ends with \r\n */
        if(strcmp(buf,"\r\n") == 0){
            break;
        }
    }
#ifdef DEBUG_PIC
    printf("tid_%d Header complete\n",tid);


    /*Parse Server Response*/
    printf("tid_%d, Response Header\n");
#endif
    while( (length = Rio_readnb_r(&rio_web, buf, MAXLINE) ) ){
        Rio_writen_r(connfd, buf, length);
        currentLength += length;
#ifdef DEBUG_PIC
        printf("tid_%d, %s\n",tid, buf);
#endif
        /*Check for Response header*/
        char* ptr = strstr(buf,"HTTP/1.1 ");
        if(ptr == buf){
            retValue = atoi(buf + 8);
        }
        /*Response header end*/
        if(strcmp(buf,"\r\n") == 0){
            break;
        }
    }
#ifdef DEBUG_PIC
    printf("tid_%d Response complete\n",tid);

    /*Transfer web content*/
    printf("tid_%d, Content\n");
#endif

    while( (length = Rio_readlineb_r(&rio_web,buf, MAXLINE)) ){
#ifdef DEBUG_PIC
        printf("tid_%d length = %d, %s\n",tid, length, buf);
#endif
        Rio_writen_r(connfd, buf, length);
        currentLength += length;
    }
#ifdef DEBUG_PIC
    printf("tid_%d close, received %d bytes\n",tid, currentLength);
    fflush(stdout);
#endif
    format_log_entry(&clientaddr,uri2, currentLength);
    close(fd_web);
    close(connfd);

    return currentLength;
}



void clienterror(int fd, char *errnum, char *shortmsg){
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen_r(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen_r(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen_r(fd, buf, strlen(buf));
    Rio_writen_r(fd, body, strlen(body));

}

int Open_clientfd_r(char* hostname, int port){


    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return -1; /* check errno for cause of error */
    P(&lock_gethost);
    /* Fill in the server's IP address and port */
    if ((hp = gethostbyname(hostname)) == NULL){
        V(&lock_gethost);
        return -2; /* check h_errno for cause of error */
    }
    V(&lock_gethost);
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr_list[0],
	  (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;

    return clientfd;

}



/**********************************
 * Wrappers for robust I/O routines
 **********************************/
ssize_t Rio_readn_r(int fd, void *ptr, size_t nbytes)
{
    ssize_t n;

    if ((n = rio_readn(fd, ptr, nbytes)) < 0)
        return -1;
    return n;
}

ssize_t Rio_writen_r(int fd, void *usrbuf, size_t n)
{
    if (rio_writen(fd, usrbuf, n) != n)
        return -1;
    return n;
}

void Rio_readinitb_r(rio_t *rp, int fd)
{
    rio_readinitb(rp, fd);
}

ssize_t Rio_readnb_r(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
        return -1;
    return rc;
}

ssize_t Rio_readlineb_r(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
        return -1;
    return rc;
}
