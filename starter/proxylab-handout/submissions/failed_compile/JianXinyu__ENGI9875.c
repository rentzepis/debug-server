#include "csapp.h"

#include "cache.h" // cache

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp);
void write_request(int fd, char *method, char *filename, char *hostname, char *port);
void parse_url(char *url, char *hostname, char *port, char *filename);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);
void sigpipe_handler();
void *thread(void *vargp);

/* Main routine */
int main(int argc, char **argv)
{
    /* Check command line args */
    if (argc != 2){
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    cache_init();
    // ignore SIGPIPE, see CSAPP Page987 Aside
    Signal(SIGPIPE, sigpipe_handler);

    // open a listening socket
    listenfd = Open_listenfd(argv[1]);

    // execute the typical infinite server loop, CSAPP 12.3.8
    while (1) {
        clientlen = sizeof(clientaddr);
        // to avoid the potentially deadly race
        connfdp = Malloc(sizeof(int));
        // repeatedly accept a connection request
        *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        // perform a transaction
        pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}

/* 
 * catach SIGPIPE 
 */
void sigpipe_handler()
{
    Sio_error("SIGPIPE caught!");
}

void* thread(void *vargp){
    int connfd = *((int *) vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/**
 * doit - handle one HTTP request&response transaction
 *        only GET method is implemented
 * @param fd the connected descriptor on proxy
 * */
void doit(int fd) 
{
    char buf[MAXLINE];                  // general purpose buffer
    char cache_buf[MAX_OBJECT_SIZE];    // buffer of the object(HTTP response) being cached
    char buf_size = 0;                  // size of the object(HTTP response) being cached
    // elements in the request line
    char method[MAXLINE], url[MAXLINE], urlcpy[MAXLINE], version[MAXLINE];
    // elements in the URL
    char hostname[MAXLINE], port[MAXLINE], filename[MAXLINE];

    /*  rio_client is a read buffer of the request from client
        rio_server is a read buffer of the response from server */
    rio_t rio_client, rio_server;

    /* Read request line and headers */
    Rio_readinitb(&rio_client, fd);
    if (!Rio_readlineb(&rio_client, buf, MAXLINE))
        return;
    printf("%s", buf);

    /* parse request */
    sscanf(buf, "%s %s %s", method, url, version);
    strcpy(urlcpy, url); // keep an original URL
    // if the client request another method, e.g., POST, 
    // send the client an error message and return to the main routine,
    // which then closes the connection and awaits the next connection request
    if (strcasecmp(method, "GET")) {                     
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }

    /* simply read and ignore any request headers */
    read_requesthdrs(&rio_client);                              

    const char *cached_obj = cache_find(urlcpy);
    /* if find the requested object in the cache */
    if( cached_obj ){
        Rio_writen(fd, cached_obj, strlen(cached_obj));
        return;
    }

    /* if not find, proxy requests data from server */

    /* Parse URL from GET request */
    parse_url(url, hostname, port, filename);

    // for the server, the proxy serves as client
    int clientfd = Open_clientfd(hostname, port);
    if(clientfd < 0){
        printf("Connection from proxy to server fails.\n");
        return;
    }

    // send request to server
    write_request(clientfd, method, filename, hostname, port);
    
    // associate clientfd with rio_server
    Rio_readinitb(&rio_server, clientfd);

    // read response from the server and send server response to client
    // why while loop is a must? cause we read line by line until empty line
    int n;
    while( (n = Rio_readlineb(&rio_server, buf, MAXLINE)) != 0 ){
        buf_size += n;
        if (buf_size < MAX_OBJECT_SIZE) {
            strcat(cache_buf, buf);
        }
        // send back to the client
        Rio_writen(rio_client.rio_fd, buf, n);
    }
    
    Close(clientfd);

    /* store it in cache */
    if (buf_size < MAX_OBJECT_SIZE) {
        cache_put(urlcpy, cache_buf);
    }
}


/*
 * read_requesthdrs - read HTTP request headers
 * 
 */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    /* the empty text line that terminates the request headers 
     * consists of a carriage return and line feed pair */
    while(strcmp(buf, "\r\n")) {        
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

/**
 * parse_url - parse URL into hostname, port, filename
 *  URL = http:// + hostname + : + port + filename
 * */
void parse_url(char *url, char *hostname, char *port, char *filename) 
{
    char *ptr;
    // ignore http://
    if( strstr(url, "http://") ) url = url + 7;

    // find the filename
    if ((ptr = index(url, '/'))) {
        strcpy(filename, ptr);
        *ptr = '\0';
    } 
    else {
        strcpy(filename, "/index.html"); // default home page
    }

    // find the port
    if ((ptr = index(url, ':'))) {
        strcpy(port, ptr+1);
        *ptr='\0';
    }

    // the rest is the hostname
    strcpy(hostname, url);
}

/*
 * write_request - write HTTP request line and headers to the server
 */
void write_request(int fd, char *method, char *filename, char *hostname, char *port) 
{
    char buf[MAXLINE];

    sprintf(buf, "%s %s HTTP/1.0\r\n", method, filename);
    // user is very likely not to provide port
    if (port[0] != '\0')
        sprintf(buf, "%sHOST: %s:%s\r\n", buf, hostname, port);
    else 
        sprintf(buf, "%sHOST: %s\r\n", buf, hostname);  
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    Rio_writen(fd, buf, strlen(buf));
}

/*
 * clienterror - returns an error message to the client
 */
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
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
