/*
 * proxy.c - CS:APP Web proxy
 *
 *****************************
 * name:   Li Xinyu          *
 * ID:     515030910292      *
 * email:  1091161455@qq.com *
 *****************************
 *
 *
 * Description:
 *      For this proxy, recive request from client, deal with the request to parse hostname、uri、port, 
 * then forward request to web server. After conncted to the server, use rio_readnb to read from server
 * and use rio_writen to write to the client. Close both descriptor while all contents have been 
 * read and written and then write the log file. 
 *      Use function Signal to ignore signal SIGPIPE and use semaphore and mutex to solve problem caused by thread-unsafe in the procedure of getting 
 * host by name and writting log file. 
 */ 

#include "csapp.h"
#include "string.h"

struct sockaddr_in sockaddr;
FILE* logfile;
sem_t slog;
sem_t shost;


/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);
void *thread(void *vargp);
void doit(int fd, int *size, char *uri);
int forward_server(char *hostname, char *pathname, int port, char *version);
int open_clientfd_ts(char *hostname, int port, struct hostent *host);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);
void clienterror(int fd, char* cause, char *errnum, char* shortmsg, char* longmsg);



/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
    int listenfd, *connfdp, port;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    pthread_t tid;

    Sem_init(&slog, 0, 1);
    Sem_init(&shost, 0, 1);

    /*ignore signal SIGPIPE*/
    Signal(SIGPIPE, SIG_IGN);

    /*open log file*/
    if((logfile=Fopen("./proxy.log","a"))==NULL){
        fprintf(stderr, "Can't open logfile\n");
        exit(0);
    }

    /* Check arguments */
    if (argc != 2) {
       fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
       exit(0);
    }
    port = atoi(argv[1]);

    listenfd = Open_listenfd(port);

    while(1){
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*) &sockaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    Fclose(logfile);
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
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
              char *uri, int size)
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
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}

/*
 * thread
 */
void *thread(void *vargp)
{
    //printf("waiting to get\n");
    int connfd = *((int*)vargp);
    int size;
    char logstring[MAXLINE], uri[MAXLINE];

    Pthread_detach(pthread_self());
    Free(vargp);

    /*deal with request*/
    doit(connfd, &size, uri);
    
    /*format log file*/
    format_log_entry(logstring, &sockaddr, uri, size);
    sprintf(logstring, "%s\n", logstring);

    /*write log file using metux*/
    P(&slog);
    Fputs(logstring, logfile);
    fflush(logfile);
    V(&slog);

    Close(connfd);
    return NULL;
}


/*
 * 
 */
void doit(int fd, int *size, char *uri)
{
    int port, clientfd;
    char buf[MAXLINE], method[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE];
    rio_t rio, _rio;
    char *pos;
    int clength = 0;
    size_t n;

    Rio_readinitb(&rio, fd);
    Rio_readlineb_w(&rio, buf, MAXLINE);
    if(sscanf(buf, "%s %s %s", method, uri, version)<2){
        clienterror(fd, method, "400", "Bad request",
                    "Proxy can't understand this request");
        return;
    }
    if(strcasecmp(method, "GET")){
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy hasn't implement this method");
        return;
    }
    //if(version[0] == '\0')
        strcpy(version, "HTTP/1.0");

    /*ignore request header*/
    while(Rio_readlineb_w(&rio, buf, MAXLINE)>2);
    
    /*parse URI from GET request*/
    if(parse_uri(uri, hostname, pathname, &port) < 0){
        clienterror(fd, method, "400", "Bad request",
                    "Proxy can't parse uri from this request");
        return;
    }

    /*forward request to web server to establish connection*/ 
    if((clientfd = forward_server(hostname, pathname, port, version)) < 0){
        clienterror(fd, method, "404","Not Found",
                    "Proxy can't forward to server");
        return;
    }
    
    Rio_readinitb(&_rio, clientfd);
    /*read response header to get length*/
    while((n = Rio_readlineb_w(&_rio, buf, MAXLINE))>2){
        if((pos = strstr(buf, "Content-Length:")) != NULL){
            pos = pos + 15;
            sscanf(pos,"%d\n", &clength);
        }
        Rio_writen_w(fd, buf, n);
    }
    Rio_writen_w(fd,buf, n); //terminal line
    
    /*read response body*/
    if(clength > 0){
        while((n = Rio_readnb_w(&_rio, buf, MAXLINE)) > 0)
            Rio_writen_w(fd, buf, n);
    }
    else{
        while((n = Rio_readnb_w(&_rio, buf, MAXLINE)) > 0){
            Rio_writen_w(fd, buf, n);
            clength += n;
        }
    }
    *size = clength;
    Close(clientfd);
    return;
}


int forward_server(char *hostname, char *pathname, int port, char *version)
{
    char buf[MAXLINE];    
    int clientfd;
    struct hostent host;

    //printf("%s:%d /%s %s\n",hostname, port, pathname, version);
    if((clientfd = open_clientfd_ts(hostname, port, &host))<0)  
        return -1;
    /* write request to server */  
    sprintf(buf, "GET /%s %s\r\n", pathname, version);  
    Rio_writen_w(clientfd, buf, strlen(buf));
    sprintf(buf,"Host: %s\r\n\r\n",hostname); 
    Rio_writen_w(clientfd, buf, strlen(buf));
    
    return clientfd;
}


int open_clientfd_ts(char *hostname, int port, struct hostent *host){
    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        return -1; /* check errno for cause of error */

    /*use lock-and-copy techique to ensure thread-safe*/
    P(&shost);
    if ((hp = gethostbyname(hostname)) == NULL)
        return -2; /* check h_errno for cause of error */
    memcpy(host, hp, sizeof(struct hostent));
    V(&shost);

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)host->h_addr_list[0], 
      (char *)&serveraddr.sin_addr.s_addr, host->h_length);
    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
        return -1;
    
    return clientfd;
}


ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0){
        fprintf(stderr, "Rio_readnb error\n");
        return 0;
    }
    return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0){
        fprintf(stderr, "Rio_readlineb error\n");
        return 0;
    }
    return rc;
} 

void Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) != n)
        fprintf(stderr, "Rio_writen error\n");
    return;
}

/*
 * clienterror - Send an error message to client end
 */
void clienterror(int fd, char* cause, char *errnum,
                 char* shortmsg, char* longmsg)
{
    char buf[MAXLINE], body[MAXLINE];

    /*Bulid the HTTP reponse body*/
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n",body, errnum, shortmsg);  
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    
    /*Print the HTTP reponse*/
    sprintf(body, "HTPP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "Content-Type: text/html\r\n");
    Rio_writen_w(fd, buf, strlen(buf));
    sprintf(buf, "Content-Length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen_w(fd, buf, strlen(buf));
    Rio_writen_w(fd, body, strlen(body));
}
