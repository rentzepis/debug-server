/*
 * proxy.c - ICS Web proxy
 * 
 * name : 楼天驰
 * id : 522031910290
 *
 * This proxy may encounter timeout when server crash
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

#include <assert.h>

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);
void *thread(void *vargp);
void doThings(int client_fd);
int sendM(rio_t *client_rio_p, char *uri, char *method, char *http_version, int *totSize);

// global variables
sem_t mutex;

/*
 * wrapper funciton implementations	
 */
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n)
{
	ssize_t rc;
	if((rc = rio_writen(fd, usrbuf, n)) != n)
		fprintf(stderr, "%luRio writen error %d: %s\n", rc, errno, strerror(errno));
	return rc;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
	ssize_t rc;
	if((rc = rio_readnb(rp, usrbuf, n)) < 0 )
		fprintf(stderr, "%luRio readnb error %d: %s\n", rc, errno, strerror(errno));
	return rc;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen){
	ssize_t rc;
	if((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
		fprintf(stderr, "%luRio readlineb error %d : %s\n", rc, errno, strerror(errno));
	return rc;
}

int Open_clientfd_w(char *hostname, char *port)
{
	int rc;
	if((rc = open_clientfd(hostname, port)) < 0)
		fprintf(stderr, "%d Open clientfd error %d:%s\n", rc, errno, strerror(errno));
	return rc;
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
    int port = atoi(argv[1]);
    if (port < 1024 || port > 65536){
        printf("input a port within [1024, 65536]\n");
        exit(0);
    }
    // use Open because we need to exit
    int listen_fd = Open_listenfd(argv[1]);
    sem_init(&mutex, 0, 1);

    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    while (1) {
        clientlen = sizeof(clientaddr);
        int *connfd = (int*)malloc(sizeof(int));
        if (connfd == NULL) {
            fprintf(stderr, "Malloc Error: %s\n", strerror(errno));
            continue;
        }
        // use accept because we don't want to exit immediately
        *connfd = accept(listen_fd, (SA*)&clientaddr, &clientlen);
        if (*connfd < 0) {
            fprintf(stderr, "Accept Error: %s\n", strerror(errno));
            free(connfd);
            continue;
        }
        
        // create a new thread
        if (pthread_create(&tid, NULL, thread, connfd) != 0) {
            fprintf(stderr, "Pthread_create Error: %s\n", strerror(errno));
            free(connfd);
            continue;
        }
    }
    close(listen_fd);

    exit(0);
}

// Thread routine
void *thread(void *vargp)
{
    int connfd = *((int*) vargp);
    pthread_detach(pthread_self());
    free(vargp);
    doThings(connfd);
    close(connfd);
    return NULL;
}

// doThings: parse request from client, send message to server
void doThings(int client_fd)
{
    rio_t client_rio;
    char buf[MAXLINE];
    rio_readinitb(&client_rio, client_fd);

    // read request from client
    if (Rio_readlineb_w(&client_rio, buf, MAXLINE) <= 0) {
        close(client_fd);
        return;
    }

    // parse the request(method, URI, version)
    char method[MAXLINE], uri[MAXLINE], http_version[MAXLINE];
    if (sscanf(buf, "%s %s %s", method, uri, http_version) != 3) {
        fprintf(stderr, "Parse request line error: %s\n", strerror(errno));
        close(client_fd);
        return;
    }

    int totSize;
    if (sendM(&client_rio, uri, method, http_version, &totSize) < 0) {
        close(client_fd);
        return;
    }

    struct sockaddr_in peeraddr;
    socklen_t peeraddrlen = sizeof(peeraddr);
    if (getpeername(client_fd, (SA*)&peeraddr, &peeraddrlen) < 0) {
        fprintf(stderr, "getpeername failed: %s\n", strerror(errno));
        close(client_fd);
        return;
    }
    char log[MAXLINE];
    format_log_entry(log, &peeraddr, uri, totSize);

    P(&mutex);
    printf("%s\n", log);
    V(&mutex);
    close(client_fd);
}

// send message to server, and send back to client.
int sendM(rio_t *client_rio_p, char *uri, char *method, char *http_version, int *totSize)
{
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
    if (parse_uri(uri, hostname, pathname, port) < 0) {
        fprintf(stderr, "Parse url error\n");
        return -1;
    }
    
    // connect to server
    int server_fd = Open_clientfd_w(hostname, port);
    if (server_fd < 0) {
        return -1;
    }

    // initialize server rio
    rio_t server_rio;
    rio_readinitb(&server_rio, server_fd);

    // prepare the head and send
    char buf[MAXLINE];
    sprintf(buf, "%s /%s %s\r\n", method, pathname, http_version);

    if (Rio_writen_w(server_fd, buf, strlen(buf)) != strlen(buf)) {
        close(server_fd);
        return -1;
    }
    int req_write = 0, req_tmp, req_content_length = 0;
    do {
        // read new header line
        req_tmp = Rio_readlineb_w(client_rio_p, buf, MAXLINE);
        if(req_tmp < 0) {
			close(server_fd);
        	return -1;
        }
        // send to server
        req_write = Rio_writen_w(server_fd, buf, strlen(buf));
        if(req_write != strlen(buf)){
			close(server_fd);
        	return -1;
        }
        if (strstr(buf, "Content-Length"))
            req_content_length = atoi(strstr(buf, "Content-Length") + 15);
    } while (strcmp(buf, "\r\n"));

    memset(buf, 0, sizeof(buf));
    if (req_content_length > 0)
    {
        for(size_t i = 0; i < req_content_length; i++){
            if((req_tmp = Rio_readnb_w(client_rio_p, buf, 1)) > 0){
                Rio_writen_w(server_fd, buf, 1);
            }
            else if (req_tmp == 0) {
                break;
            }
            else {
                close(server_fd);
                return -1;
            }
        }
    }

    // receive info from server, read until EOF occur(or /r/n)
    *totSize = 0;
    int client_fd = client_rio_p->rio_fd;
    int res_write = 0, res_tmp, res_content_length = 0, tot = 0;

    do {
        // read new header line
        res_tmp = Rio_readlineb_w(&server_rio, buf, MAXLINE);
        if(res_tmp < 0) {
			close(server_fd);
        	return -1;
        }
        if (res_tmp == 0) {
            res_write = Rio_writen_w(client_fd, "\r\n", 2);
            break;
        }
        // send to client
        res_write = Rio_writen_w(client_fd, buf, strlen(buf));
        if(res_write != strlen(buf)){
			close(server_fd);
        	return -1;
        }
        tot += res_write;
        if (strstr(buf, "Content-Length"))
            res_content_length = atoi(strstr(buf, "Content-Length") + 15);
    } while (strcmp(buf, "\r\n"));

    memset(buf, 0, sizeof(buf));
    if (res_content_length > 0)
    {
        for(size_t i = 0; i < res_content_length; i++){
            if((res_tmp = Rio_readnb_w(&server_rio, buf, 1)) > 0){
                Rio_writen_w(client_fd, buf, 1);
                tot++;
            }
            else if (res_tmp == 0) {
                break;
            }
            else {
                close(server_fd);
                return -1;
            }
        }
    }
    close(server_fd);
    *totSize = tot;
    return 0;
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
    hostend = strpbrk(hostbegin, " :/\n\0");
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
