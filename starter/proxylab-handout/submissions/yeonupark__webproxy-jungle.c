#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000 // max cache size
#define MAX_OBJECT_SIZE 102400 // max object size

void doit(int fd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *rp);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {

    int listenfd, *clientfd; // server file descripter & client file descripter
    char hostname[MAXLINE], port[MAXLINE]; // hostname and port num of client
    socklen_t clientlen; // size of client address struct
    struct sockaddr_storage clientaddr; // client address struct

    pthread_t tid; // thread ID of peer thread

    /* Check command-line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]); // opening a listening socket
    while (1) {
        clientlen = sizeof(clientaddr);
        clientfd = Malloc(sizeof(int)); // assign each fd to its own dynamically allocated memory blocks
        *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // acceptig a connection request
        pthread_create(&tid, NULL, thread, clientfd); // creating a peer thread to handle the request
    }
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp); 

    pthread_detach(pthread_self()); // detach each thread so that its memory resources will be reclaimed when it terminates 
    Free(vargp); // free the memory block that was allocated by the main thread

    doit(connfd); 
    Close(connfd); // 
    return NULL;
}

/* handle one HTTP transaction. */
/* read and parse the request line from client and send it to server, and then send the response to client */
void doit(int clientfd) {
    int serverfd; // server socket descripter
    char request_buf[MAXLINE], response_buf[MAX_OBJECT_SIZE]; // buffer to store request and response
    char method[MAXLINE], uri[MAXLINE], path[MAXLINE]; // request method, URI, path
    char hostname[MAXLINE], port[MAXLINE]; // hostname, port num
    rio_t request_rio, response_rio; // buffered rio to deal with request from client and response from server

    /* Read Request line and headers */
    Rio_readinitb(&request_rio, clientfd); // connect client fd to request_rio
    Rio_readlineb(&request_rio, request_buf, MAXLINE); // read request line from client and store in buf
    printf("Request header: %s\n", request_buf);

    /* read request method, URI */ 
    sscanf(request_buf, "%s %s", method, uri); // extract method and URI from request line 

    /* Parse URI and extract hostname, port, path */
    parse_uri(uri, hostname, port, path);
    printf("uri: %s\n", uri);
    printf("path: %s\n", path);

    /* make new request */
    sprintf(request_buf, "%s /%s %s\r\n", method, path, "HTTP/1.0"); // re-configure request line
    printf("%s\n", request_buf); 
    sprintf(request_buf, "%sConnection: close\r\n", request_buf); // add Connection header
    sprintf(request_buf, "%sProxy-Connection: close\r\n", request_buf); // add Proxy-Connection header
    sprintf(request_buf, "%s%s\r\n", request_buf, user_agent_hdr); // add User-Agent header

    /* if the method is not GET or HEAD, send error response */
    if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
        clienterror(clientfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    /* send client request to server */
    serverfd = Open_clientfd(hostname, port); // make connection to server
    if (serverfd < 0) { // if the connection fails
        clienterror(clientfd, hostname, "404", "Not found", "Proxy couldn't connect to the server");
        return;
    }
    printf("%s\n", request_buf); 

    Rio_writen(serverfd, request_buf, strlen(request_buf)); // send request to server
    Rio_readinitb(&response_rio, serverfd); // connect serverfd to response_rio

    /* get response from server and send it to client */
    ssize_t n;
    /* send response header */
    while ((n = Rio_readlineb(&response_rio, response_buf, MAX_OBJECT_SIZE)) > 0) {
      Rio_writen(clientfd, response_buf, n);
      if (!strcmp(response_buf, "\r\n"))
        break;
    }

    /* send response body */
    while ((n = Rio_readlineb(&response_rio, response_buf, MAX_OBJECT_SIZE)) > 0) {
      Rio_writen(clientfd, response_buf, n);
    }
    Close(serverfd);
}


/* Parse HTTP URI to hostname, port, path */
void parse_uri(char *uri, char *hostname, char *port, char *path) {
    printf("parse_uri: %s\n", uri); 
    // set pointer to hostname
    char *hostname_ptr = strstr(uri, "//") != NULL ? strstr(uri, "//") + 2 : uri + 1;
    // set pointer to port
    char *port_ptr = strstr(hostname_ptr, ":");
    // set pointer to path
    char *path_ptr = strstr(hostname_ptr, "/");
    
    // if the path exists
    if (path_ptr > 0) {
        *path_ptr = '\0'; // replace the path to '\0' to end
        strcpy(path, path_ptr+1); // copy the path to path buffer
    }
    // if the port exists
    if (port_ptr > 0) {
        *port_ptr = '\0'; // replace the port to '\0' to end 
        strcpy(port, port_ptr + 1); // copy the port to port buffer
    }

    strcpy(hostname, hostname_ptr); // copy the hostname to hostname buffer
    printf("parse_uri host: %s, port: %s, path: %s\n", hostname, port, path);
}


// send an HTTP response to the client with the appropriate status code and status message
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>"); // first line of HTML page
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body); // background color
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg); // error num and short msg
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause); // cause of error
    sprintf(body, "%s<hr><em>The Proxy server</em>\r\n", body); // information of server

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg); // response line of HTTP (status code and msg)
    Rio_writen(fd, buf, strlen(buf)); // send the response to client
    sprintf(buf, "Content-type: text/html\r\n"); // set HTML content type
    Rio_writen(fd, buf, strlen(buf)); // send the type to client 
    sprintf(buf, "Content-length: %lu\r\n\r\n", strlen(body)); // set content length of HTML body
    Rio_writen(fd, buf, strlen(buf)); // send the length to client
    Rio_writen(fd, body, strlen(body)); // send the body yo client
}

/* Read HTTP request headers from client */
void read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    // read HTTP headers
    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) { // repeat until empty line (strcmp: return 0 if match)
        Rio_readlineb(rp, buf, MAXLINE); // read readline and store it in buf
        printf("%s", buf);
    }
    return;
}