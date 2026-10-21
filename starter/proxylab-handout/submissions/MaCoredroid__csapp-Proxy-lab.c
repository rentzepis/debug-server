/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);
void *thread(void *vargs);
void doit(int fd, struct sockaddr_in *csock);
int parse_cnt_len(const char *hdr, long *len);


sem_t mutex;

typedef struct {
    int thid;
    int connfd;
    struct sockaddr_in sock_in;
} thr_sock_t;

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    // Check arguments 
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    // signal(SIGPIPE, SIG_IGN);
    Sem_init(&mutex, 0, 1);

    int connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t client_len;
    struct sockaddr_storage client_addr;
    pthread_t tid;

    int listenfd = open_listenfd(argv[1]);
    while (1) {
        client_len = sizeof(client_addr);
        connfd = Accept(listenfd, (SA *)&client_addr, &client_len);
        int flags = NI_NUMERICHOST | NI_NUMERICSERV;
        Getnameinfo((SA *) &client_addr, client_len, hostname, MAXLINE, port, MAXLINE, flags);
		//pack thr_sock_t
        struct sockaddr_in *csock_in;
        thr_sock_t *thr_sock = Malloc(sizeof(thr_sock_t));
        thr_sock->connfd = connfd;
        csock_in = &thr_sock->sock_in;

        memset(csock_in, 0, sizeof(struct sockaddr_in));
        csock_in->sin_family = AF_INET;
        inet_pton(AF_INET, hostname, &(csock_in->sin_addr.s_addr));
        csock_in->sin_port = htons((short) atoi(port));
        Pthread_create(&tid, NULL, thread, thr_sock);
    }

    exit(0);
}

//thread
void *thread(void *vargs)
{
    thr_sock_t thr_sock = *((thr_sock_t *)vargs);
    int connfd = thr_sock.connfd;
    struct sockaddr_in *csock_in = &thr_sock.sock_in;
    Pthread_detach(pthread_self());
    Free(vargs);
    doit(connfd, csock_in);
	Close(connfd);
    return NULL;
}

// doit - proxy routine
void doit(int cfd, struct sockaddr_in *csock)
{
    rio_t crio, srio;
    rio_readinitb(&crio, cfd);
    int sfd=0;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
	char body[MAXBUF];
	char log[MAXLINE];
    ssize_t flag = 1;
	size_t flow = 0;
    long bodysize = 0;
    // Get  Http request 
    if ((flag = rio_readlineb(&crio, buf, MAXLINE)) < 1) 
	{
        return;
    }
    sscanf(buf, "%s %s %s", method, uri, version);
    if (parse_uri(uri, hostname, pathname, port) == -1) 
	{
        printf("Error\n");
        return;
    }
    if ((sfd = open_clientfd(hostname, port)) < 0)
	{		
		return;
	}
    // Forward request to server 
    sprintf(buf, "%s /%s %s\r\n", method, pathname, version);
    if ((flag = rio_writen(sfd, buf, strlen(buf))) == -1) 
	{
		Close(sfd);
		printf("ERROR\n");
		return;
	}
    while (strcmp(buf, "\r\n")) 
	{
        if ((flag = rio_readlineb(&crio, buf, MAXLINE)) == -1) 
		{
			Close(sfd);
			printf("ERROR\n");
			return;
		}
        if (flag == 0) 
		{
			Close(sfd);
			printf("ERROR\n");
			return;
		}
        if (strncasecmp(buf, "Content-Length", 14) == 0) 
		{
			char cnt[30];
			sscanf(buf, "%s %ld", cnt, &bodysize);
			printf("Parse Content Length : %ld\n", bodysize);
        }
        if ((flag = rio_writen(sfd, buf, flag)) == -1) 
		{
			Close(sfd);
			printf("ERROR\n");
			return;
		}
    }
    size_t readsize = MAXBUF - 1 > bodysize ? bodysize : MAXBUF - 1;
    while (bodysize > 0) 
	{
        if ((flag = rio_readnb(&crio, body, readsize)) == -1) 
		{
			Close(sfd);
			printf("ERROR\n");
			return;
		}
        if (flag == 0) 
		{
			break;
		}
        if((flag = rio_writen(sfd, body, flag)) == -1) 
		{
			Close(sfd);
			printf("ERROR\n");
			return;
		}
        bodysize -= flag;
        readsize = MAXBUF - 1 > bodysize ? bodysize : MAXBUF - 1;
    }
    if (flag == -1) 
	{
		Close(sfd);
		printf("ERROR\n");
		return;
	}

    // Get Http response from server and forward to client 
    rio_readinitb(&srio, sfd);
    if ((flag = rio_readlineb(&srio, buf, MAXLINE)) == -1)
	{
		Close(sfd);
		printf("ERROR\n");
		return;
	}
    while (strcmp(buf, "\r\n")) 
	{
        
		if (strncasecmp(buf, "Content-Length", 14) == 0) 
		{
			char cnt[30];
			sscanf(buf, "%s %ld", cnt, &bodysize);
			printf("Parse Content Length : %ld\n", bodysize);
        }

        if ((flag = rio_writen(cfd, buf, flag)) == -1)
		{
			Close(sfd);
			printf("ERROR\n");
			return;
		}
        flow += flag;
        if ((flag = rio_readlineb(&srio, buf, MAXLINE)) == -1) 
		{
			Close(sfd);
			printf("ERROR\n");
			return;
		}
        if (flag == 0) 
		{
			break;
		}
    }
    if (flag == -1)
	{
		Close(sfd);
		printf("ERROR\n");
		return;
	}
    if ((flag = rio_writen(cfd, "\r\n", strlen("\r\n"))) == -1)
	{
		Close(sfd);
		printf("ERROR\n");
		return;
	}
    flow += 2;
    readsize = 1;
    while (bodysize > 0) 
	{
       
        if ((flag = rio_readnb(&srio, body, readsize)) == -1) 
		{
			break;
		}
        if(flag == 0)
		{
			break;
		}
        if ((flag = rio_writen(cfd, body, readsize)) == -1) 
		{
			break;
		}
        bodysize -= flag;
        flow += flag;
        if(readsize > flag)
		{
			break;
		}
    }
    if (flag == -1)
	{
		Close(sfd);
		printf("ERROR\n");
		return;
	}
    Close(sfd);
    format_log_entry(log, csock, uri, flow);
    P(&mutex);
    printf("%s\n", log);//protect log print
    V(&mutex);
    return;

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


