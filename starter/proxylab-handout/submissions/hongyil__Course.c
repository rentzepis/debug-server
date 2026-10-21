/**********************************************************************
 * Name: Hongyi Liang
 * Andrew ID: hongyil
 *
 * proxy.c: A simple, HTTP proxy that can finish basic HTTP operation
 *          and deal with multiple concurrent connections.
 *			note: web cache is not completed
 * 			request headers are declared as const for convenience
 * reference:tiny.c
 **********************************************************************/

#include "csapp.h"
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* function prototype */
void *thread(void *vargp);
void proxy(int connfd);
void send_request(int serverfd,char* host, char* request, char* buf);
void clienterror(int fd, char *cause, char *errnum, 
	             char *shortmsg, char *longmsg);
void parse_url(char *url, char *host, char* port,char* request);

/* request headers declaration */
static char* header_user_agent = "Mozilla/5.0"
                                    " (X11; Linux x86_64; rv:10.0.3)"
                                    " Gecko/20120305 Firefox/10.0.3\r\n";
static char* connection_header = "Connection: close\r\n";
static char* proxy_conn_header = "Proxy-Connection:close\r\n";

/* 
 * main: initialize and open a new connection (connfdp)
 *		 use concurrent programming with threads
 *       need to block SIGPIPE signal which will terminate the process
 * reference: csapp textbook and tiny.c
 */
int main(int argc, char **argv){

    int listenfd, *connfdp;
    socklen_t clientlen;
    char host[MAXLINE], port[MAXLINE];
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    
    if (argc != 2){
    	fprintf(stderr, "usage: %s <port>\n", argv[0]);
    	exit(0);
    }

    Signal(SIGPIPE, SIG_IGN);
    listenfd=Open_listenfd(argv[1]);

    while (1){
    	clientlen = sizeof(struct sockaddr_storage);
    	connfdp = Malloc(sizeof(int));
    	*connfdp = Accept(listenfd, (SA *)&clientaddr,&clientlen);
    	Getnameinfo((SA *)&clientaddr, clientlen, host, MAXLINE, port, MAXLINE, 0);
    	printf("Accepted connection from %s:%s\n", host, port);
    	Pthread_create(&tid, NULL, thread, connfdp);
    }
    Close(listenfd);

    return 0;
}

/* 
 * thread: to create new thread
 *         use Pthread_detach() to release space
 * reference: csapp textbook and tiny.c
 */
void *thread(void *vargp){

	int connfd = *((int *)vargp);	
	Pthread_detach(pthread_self());
	Free(vargp);
	proxy(connfd);
	Close(connfd);
	return NULL;
}

/* 
 * proxy: proxy will complete basic http operations
 *        need to check if valid http request
 *        parse the url as host and port
 * reference: csapp textbook and tiny.c
 */
void proxy(int connfd){

	int n;
	int serverfd;
	char buf[MAXLINE],request[MAXLINE];
	char method[MAXLINE],url[MAXLINE],version;
	char host[MAXLINE], port[MAXLINE];
	rio_t client_rio,server_rio;

	Rio_readinitb(&client_rio,connfd);

	if (rio_readlineb(&client_rio,buf,MAXLINE) <= 0){
		Close(connfd);
		return;
	}

	if (sscanf(buf, "%s %s HTTP/1.%c", method, url, &version)!= 3
               || (version != '0' && version != '1')){
        clienterror(connfd, buf, "400", "Bad Request",
                    "Tiny received a malformed request");
    	Close(connfd);
        return;
    }

    if (strncmp(method, "GET", sizeof("GET"))){
        clienterror(connfd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        Close(connfd);
        return;
    }

	parse_url(url, host, port, request);

    if ((serverfd=Open_clientfd(host, port))<0){
    	clienterror(connfd, url, "505", "Not Supported",
          			"Not correct http protocol");
        Close(connfd);
        return;
    }else{
    	Rio_readinitb(&server_rio,serverfd);
    }

    send_request(serverfd,host, request, buf);
    while ((n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0){
		Rio_writen(connfd,buf,n);
	}
    Close(serverfd);

	return;
}

/* 
 * clienterror: returns an error message to the client
 * reference: tiny.c
 */
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg){

    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
            "<!DOCTYPE html>\r\n" \
            "<html>\r\n" \
            "<head><title>Tiny Error</title></head>\r\n" \
            "<body bgcolor=\"ffffff\">\r\n" \
            "<h1>%s: %s</h1>\r\n" \
            "<p>%s: %s</p>\r\n" \
            "<hr /><em>The Tiny Web server</em>\r\n" \
            "</body></html>\r\n", \
            errnum, shortmsg, longmsg, cause);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 %s %s\r\n" \
            "Content-Type: text/html\r\n" \
            "Content-Length: %zu\r\n\r\n", \
            errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

/* 
 * parse_url: to parse url into different parts and rebuild the header
 * 			  default port:80
 *            need to locate the host and specify the port 
 *            ignore the dynamic process
 * reference: csapp textbook and tiny.c
 */
void parse_url(char *url, char *host, char* port,char* request){
    
    char *ptr_host, *ptr_tmp;
    char *ptr_path = url;
    
    strcpy(port, "80");
    
    if (!strstr(url, "cgi-bin")){ 
        
        if ((ptr_host = strstr(url,"http://"))){
            ptr_host += 7;
        }else{
        	ptr_host = url;
        }

        ptr_path = strchr(ptr_host,':');
        ptr_tmp = strchr(ptr_host,'/');

        if(ptr_path){    
            *ptr_path = '\0';
            *ptr_tmp = '\0';
            strcpy(host,ptr_host);
            strcpy(port,ptr_path+1);
            *ptr_tmp = '/';    
        	strcpy(host, ptr_host);
        	strcpy(request, ptr_tmp);
    	}
    }
    return;
}

/* 
 * send_request: rebuild the request header
 *               each line ends with "\r\n";
 *               the header ends with "\r\n\r\n"
 * reference: csapp textbook and tiny.c
 */
void send_request(int serverfd,char* host, char* request, char* buf){

    sprintf(buf,"GET %s HTTP/1.0\r\n",request);
    Rio_writen(serverfd,buf, strlen(buf));
    sprintf(buf,"Host: %s\r\n",host);
    Rio_writen(serverfd, header_user_agent, strlen(header_user_agent));
    Rio_writen(serverfd, connection_header, strlen(connection_header));
    Rio_writen(serverfd, proxy_conn_header, strlen(proxy_conn_header));
	Rio_writen(serverfd, "\r\n", strlen("\r\n"));

	return;
}

