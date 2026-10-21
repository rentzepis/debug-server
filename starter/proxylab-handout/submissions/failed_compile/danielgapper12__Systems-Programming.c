#include "csapp.h"

#include <stdio.h>

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void handle_client(int connfd);

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr; // Generic sockaddr for client

    printf("%s", user_agent_hdr);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(0);
    }

    listenfd = Open_listenfd(argv[1]); // Create a listening socket

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // Accept a client connection
        handle_client(connfd); // Handle the client request
        Close(connfd); // Close the connection
    }

    return 0;
}

void handle_client(int connfd) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[10], path[MAXLINE];
    if (parse_uri(uri, hostname, port, path) < 0) {
        //clienterror(fd, uri, "400", "Bad Request", "Tiny couldn't parse the URI");
        //print("Bad request");
        return;
    }

    printf("Parsed URI - Hostname: %s, Port: %s, Path: %s\n", hostname, port, path);

    //char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    rio_t rio;

    Rio_readinitb(&rio, connfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        return;
    }

    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) { // Only support GET method
        clienterror(connfd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    // problem here
    if (parse_uri(uri, hostname, port, path) < 0) {
        clienterror(connfd, uri, "400", "Bad Request", "Invalid URI");
        return;
    }

    // Connect to the target server
    int serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(connfd, hostname, "404", "Not Found", "Unable to connect to server");
        return;
    }

    // Forward the request to the server
    char request[MAXLINE];
    sprintf(request, "GET %s HTTP/1.0\r\n", path);
    Rio_writen(serverfd, request, strlen(request));

    // Forward the response back to the client
    rio_t server_rio;
    Rio_readinitb(&server_rio, serverfd);
    ssize_t n;
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        Rio_writen(connfd, buf, n);
    }

    Close(serverfd); // Close the server connection
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    // Build the HTTP response body
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Server</em>\r\n", body);

    // Send the HTTP response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %lu\r\n\r\n", strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
