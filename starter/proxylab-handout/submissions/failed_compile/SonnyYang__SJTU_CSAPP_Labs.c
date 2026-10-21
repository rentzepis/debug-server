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

FILE *fp;
sem_t mutex;
sem_t mutex2;
sem_t mutex3;
int nThread;
/*
 * Function prototypes
 */
int parse_uri(char *uri, char *hostname, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

void *thread(void *vargp);
void doit(int connfd, struct sockaddr_in sockaddr);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) ;
int Rio_writen_w(int fd, void *usrbuf, size_t n);
int open_clientfd_ts(char *hostname, int port);

/* 
 * main - Main routine for the proxy program 
 */
int main(int argc, char **argv)
{
	nThread = 0;
	signal(SIGPIPE, SIG_IGN);
	Sem_init(&mutex, 0, 1);
	Sem_init(&mutex2, 0, 1);
	Sem_init(&mutex3, 0, 1);
	int listenfd, *connfdp, port;
	struct sockaddr_in *clientaddrp;
	socklen_t clientlen = sizeof(struct sockaddr_in);
	pthread_t tid;

    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }
	port =  atoi(argv[1]);
	
	listenfd = Open_listenfd(port);
	//log file
	fp = fopen("proxy.log", "w");
	while(1){
		//store *connfdp and *(struct sockaddr_in) in one block
		connfdp = malloc(sizeof(int)+sizeof(struct sockaddr_in));
		clientaddrp = (void*)connfdp + sizeof(int);
		*connfdp = Accept(listenfd, (SA*)clientaddrp, &clientlen);
		P(&mutex3);
		printf("connection on , connected: %d\n", ++nThread);
		fflush(stdout);
		V(&mutex3);
		pthread_create(&tid, NULL, thread, (void*)connfdp);
	}
	fclose(fp);
    exit(0);
}

void *thread(void *vargp){
	int connfd = *((int*)vargp);
	struct sockaddr_in clientaddr = *((struct sockaddr_in*)(vargp+sizeof(int)));
	pthread_detach(pthread_self());
	//free memory of *connfdp and *(struct sockaddr_in)
	free(vargp);
	doit(connfd, clientaddr);
	Close(connfd);
	P(&mutex3);
	printf("connection off, connected: %d\n", --nThread);
	fflush(stdout);
	V(&mutex3);
	return NULL;
}

void doit(int connfd, struct sockaddr_in sockaddr){
	rio_t rioClient;
	rio_t rioServer;
	size_t n=0;
	int port, proxyfd, size = -1;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], host[MAXLINE], path[MAXLINE], logstring[MAXLINE];

	//read request line from client
	Rio_readinitb(&rioClient, connfd);
	Rio_readlineb_w(&rioClient, buf, MAXLINE);
	if(sscanf(buf, "%s %s %s", method, uri, version) != 3){
		printf("wrong request line: %s\n", buf);
		return;
	}
	if(parse_uri(uri, host, path, &port) == -1){
		printf("wrong uri: %s\n", buf);
		return;
	}
	//send request line to server
	proxyfd = open_clientfd_ts(host, port);
	if(proxyfd == -1) return;	//failed to connect
	sprintf(buf, "%s /%s %s\r\n", method, path, version);
	if(Rio_writen_w(proxyfd, buf, strlen(buf)) == -1){
		Close(proxyfd);
		return;
	}
	//read request header from client and send it to server
	while(strcmp(buf, "\r\n")){
		Rio_readlineb_w(&rioClient, buf, MAXLINE);	
		if(Rio_writen_w(proxyfd, buf, strlen(buf)) == -1){
			Close(proxyfd);
			return;
		}
	}

	//read response line from server and send it to client
	//according to response line, deal with the rest info
	Rio_readinitb(&rioServer, proxyfd);
	n = Rio_readlineb_w(&rioServer, buf, MAXLINE);
	if(Rio_writen_w(connfd, buf, n) == -1){
		Close(proxyfd);
		return;
	}

	if(!strcmp(buf, "HTTP/1.1 200 OK\r\n")){
		int chunked = 0;
		while(strcmp(buf, "\r\n")){
			n = Rio_readlineb_w(&rioServer, buf, MAXLINE);
			if(strstr(buf, "Content-Length")) size = atoi(buf+16);
			if(strstr(buf, "Transfer-Encoding: chunked")) chunked = 1;

			if(Rio_writen_w(connfd, buf, n) == -1){
				Close(proxyfd);
				return;
			}
		}
		//chunked
		if(!chunked){
			//logging
			P(&mutex2);
			format_log_entry(logstring, &sockaddr, uri, size);
			fprintf(fp, "%s", logstring);
			fflush(fp);
			V(&mutex2);
			while(size != 0){
				int n = (size < (MAXLINE-1)) ? size : (MAXLINE-1);
				if (Rio_readnb_w(&rioServer, buf, n) > 0){			
					if(Rio_writen_w(connfd, buf, n) == -1){
						Close(proxyfd); 
						return;
					}
				}
				size -= n;
			}
		}else{
			int logSize = 0;
			while(1){
				//length
				n = Rio_readlineb_w(&rioServer, buf, MAXLINE);
				if(Rio_writen_w(connfd, buf, n) == -1){
					Close(proxyfd);
					return;
				}
				size = strtol(buf, NULL, 16);
				logSize += size;
				if(size == 0) break;
				//content
				while(size != 0){
					int n = (size < (MAXLINE-1)) ? size : (MAXLINE-1);
					if (Rio_readnb_w(&rioServer, buf, n) > 0){			
						if(Rio_writen_w(connfd, buf, n) == -1){
							Close(proxyfd); 
							return;
						}
					}
					size -= n;
				}
				//the empty line at the end
				n = Rio_readlineb_w(&rioServer, buf, MAXLINE);
				if(Rio_writen_w(connfd, buf, n) == -1){
					Close(proxyfd);
					return;
				}
			}
			//logging
			P(&mutex2);
			format_log_entry(logstring, &sockaddr, uri, logSize);
			fprintf(fp, "%s", logstring);
			fflush(fp);
			V(&mutex2);
		}
	}else{
		//no content
		while(strcmp(buf, "\r\n")){
			n = Rio_readlineb_w(&rioServer, buf, MAXLINE);
			if(Rio_writen_w(connfd, buf, n) == -1){
				Close(proxyfd); 
				return;
			}
		}
	}
	Close(proxyfd);
}


//wrappered rio_readlineb function, it just print the error message when mistake occurs
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t rc;
    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
		puts("Rio_readlineb error");
    return rc;
}

//wrappered rio_readnb function, it just print the error message when mistake occurs
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
    ssize_t rc;
    if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
		puts("Rio_readnb error");
    return rc;
}

//wrapped rio_writen function, it just print error message when mistake occurs
int Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) != n){
		puts("Rio_writen error");
		return -1;
	}
	return 1;
}

int open_clientfd_ts(char *hostname, int port) 
{
    int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return -1; /* check errno for cause of error */

    /* Fill in the server's IP address and port */

	P(&mutex);

    if ((hp = gethostbyname(hostname)) == NULL){
		V(&mutex);
		return -2; /* check h_errno for cause of error */
	}

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr_list[0], 
	  (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0){
		V(&mutex);
		return -1;
	}
	V(&mutex);
    return clientfd;
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
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d\n", time_str, a, b, c, d, uri, size);
}
