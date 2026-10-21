/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS:
 *     Andrew Carnegie, ac00@cs.cmu.edu
 *     Harry Q. Bovik, bovik@cs.cmu.edu
 *
 * This is a basic proxy that simply sends requests from client
 * to server, and then transfer data from server to client.
 * Use multithreads to do the work.
 * Maximum number of threads is NTHREADS as below.
 *Name:Bo-Han Huang
 *ID:515030910252
 */

#include "csapp.h"



// sbuf:similar to CSAPP:671
typedef struct{
    int *conbuf;
    struct sockaddr_in *adrbuf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
}sbuf_t;

/*
 * Function prototypes
 * Detailed descriptions are above their definition
 */
void *thread(void* vargp);
int open_clientfd_ts(char* hostname, int port);
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int conitem,struct sockaddr_in addritem);
static void init_mutex(void);
void sbuf_remove(sbuf_t *sp,int* rtcon,struct sockaddr_in* rtaddr);
void doit(int clientfd,struct sockaddr_in clientaddr);
void writelog(char* log);
ssize_t Rio_readn_w(rio_t *rp, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

/*Global variables*/
sem_t mutex;   /* mutex for open_clientfd_ts()*/
sem_t mutex_w;   /* mutex for writelog()*/
sbuf_t sbuf;/*buf for producer-consumer*/

/*
 * main - Main routine for the proxy program
 * Mostly referenced to CSAPP:674 675
 */
int main(int argc, char **argv)
{
    int listenfd, port,i,connfd;
    socklen_t clientlen = sizeof(struct sockaddr_in);
    struct sockaddr_in clientaddr;
    pthread_t tid;

    // SIGPIPE to SIG_IGN
    Signal(SIGPIPE, SIG_IGN);
    init_mutex();
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }    
    port = atoi(argv[1]);
    sbuf_init(&sbuf, 128);    
    listenfd = open_listenfd(port);

    for(i=0; i<4; i++)//4 threads
        Pthread_create(&tid, NULL, thread, NULL);

    while(1){
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        sbuf_insert(&sbuf, connfd,clientaddr);/*Insert connfd and client address to buffer*/
    }
    
}
/*init_mutex-initialize the mutexs*/
static void init_mutex(void)
{
    Sem_init(&mutex, 0, 1);
    Sem_init(&mutex_w, 0, 1);
}

/*Thread work similar to CSAPP:675*/
void *thread(void *vargp){
    Pthread_detach(Pthread_self());
    while(1){
        int con;
        struct sockaddr_in addr;
        sbuf_remove(&sbuf,&con,&addr);/*Remove connfd from buffer*/
        doit(con,addr);/*Service client*/
    }
    return NULL;
}

/*doit- requests and transfers done here*/
void doit(int clientfd,struct sockaddr_in clientaddr){
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], httpv[MAXLINE];
    char message[MAXLINE],host[MAXLINE], path[MAXLINE];
    rio_t clientRio, serverRio;
    int serverfd, port;
    int count,sum = 0;
    /*From client*/
    Rio_readinitb(&clientRio, clientfd);
    Rio_readlineb_w(&clientRio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, httpv);
    // We only support GET
    if(strcasecmp(method, "GET")){
        clienterror(clientfd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
        return;
    }
    parse_uri(uri, host, path, &port);
     
    sprintf(message, "%s %s %s\r\n", method, path, httpv);
    /*copy all message*/
    while(strcmp(buf, "\r\n") != 0){
        Rio_readlineb_w(&clientRio, buf, MAXLINE);
        strcat(message, buf);
    }
    /*Write to server*/
    serverfd = open_clientfd_ts(host, port);
    Rio_writen_w(serverfd, message, sizeof(message));

    /*From Server */
    Rio_readinitb(&serverRio, serverfd);
    while((count = Rio_readn_w(&serverRio, buf, MAXLINE)) > 0){
        Rio_writen_w(clientfd, buf, count);
        sum += count;     
    }
    // Format log
    format_log_entry(buf, &clientaddr, uri, count);
    writelog(buf);
    Close(serverfd);
    Close(clientfd);
    return;
}



// From CSAPP:671 672
/*Create an empty, bounded, shared FIFO buffer with n slots*/
void sbuf_init(sbuf_t *sp, int n){
    sp->conbuf = Calloc(n, sizeof(int));
    sp->adrbuf=Calloc(n,sizeof(struct sockaddr_in));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

/*Clean up buffer sp*/
void sbuf_deinit(sbuf_t *sp){
    Free(sp->conbuf);
    Free(sp->adrbuf);
}

/*Insert item onto the rear of shared buffer sp*/
void sbuf_insert(sbuf_t *sp, int conitem,struct sockaddr_in adritem){
    P(&sp->slots);
    P(&sp->mutex);
    sp->conbuf[(++sp->rear)%(sp->n)] = conitem;
    sp->adrbuf[(sp->rear)%(sp->n)]=adritem;
    V(&sp->mutex);
    V(&sp->items);
}

/*Remove and return the first item from buffer sp*/
void sbuf_remove(sbuf_t *sp,int* rtcon,struct sockaddr_in* rtaddr){
    P(&sp->items);
    P(&sp->mutex);
    *rtcon = sp->conbuf[(++sp->front)%(sp->n)];
    *rtaddr=sp->adrbuf[(sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return;
}
/*open_clientfd_ts-safe version of open_clientfd*/
int open_clientfd_ts(char* hostname, int port){
    P(&mutex);
    int fd;
    while((fd = open_clientfd(hostname, port)) < 0);   
    V(&mutex);
    return fd;
}

/*Rio_readn_w:print message version of readn*/
ssize_t Rio_readn_w(rio_t *rp, void *usrbuf, size_t n){
    ssize_t rc;
    if ((rc = rio_readnb(rp, usrbuf, n)) < 0){
        printf("Error with rio_readnb.\n");
        return 0;
    }
    return rc;
}

/* Rio_readlineb_w-print message version of readlineb*/
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen){
    ssize_t rc;
    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0){
        printf("Error with rio_readlineb.\n");
        return -1;
    }
    return rc;
}

/*Rio_writen_w-print message version of writen*/
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n){
    ssize_t rc;
    if ((rc = rio_writen(fd, usrbuf, n)) != n){
        printf("Error with rio_writen.\n");
        return 0;
    }
    return n;
}

//Using lock-and-copy similar to CSAPP:681
void writelog(char* log){
    P(&mutex_w);
    FILE* file = Fopen("proxy.log", "a");
    fprintf(file, "%s", log);
    Fclose(file);
    V(&mutex_w);
    return;
}

//From CSAPP:641
/*clienterror-sprint the error message*/
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];

	/*Build the HTTP response body*/
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Ting Web server</em>\r\n", body);

	/*Print the HTTP response*/
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
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
     char tmp[MAXLINE];
    sprintf(tmp, "/%s", pathname);
    strcpy(pathname, tmp);
    printf("pathname = %s\n", pathname);
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

