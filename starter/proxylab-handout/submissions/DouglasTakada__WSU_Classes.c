#include <stdio.h>
#include "csapp.h"

/*Commands to Run:
./tiny 4500
./proxy 4501

telnet localhost 4501
nc localhost 4501
curl -v --proxy http://localhost:4501 http://localhost:4500/home.html
www.google.com
www.bing.com
www.wiki.com

Correct inputs:
GET http://localhost:4500/text.html HTTP/1.0
GET http://localhost/text.html HTTP/1.0
GET http://localhost:4500/home.html HTTP/1.0
GET http://localhost/home.html HTTP/1.0
GET http://localhost/home.html HTTP/1.1

Incorrect inputs:
SET http://localhost:4500/text.html HTTP/1.0
GET text.html HTTP/1.0

*/

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from %s, %s\n", hostname, port);
        doit(connfd);
        Close(connfd);
    }
}

void doit(int fd)
{
    rio_t rio;
    int clientfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char protocol[MAXLINE], host[MAXLINE], port[MAXLINE], path[MAXLINE];
    
    
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)){
        printf("Invalid HTTP request. Try again");
        return;
    }

    //Grab data from client
    printf("Recieved from Client: %s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    //Check if incoming request is a GET method
    if(strcmp(method, "GET") != 0){
        printf("Invalid HTTP request. Use method GET\n");
        fflush(stdout);
        return;
    }

    printf("Method: %s\tURI: %s\tVersion: %s\n", method, uri, version);
    
    char *tok;
    //find protocol
    if(strstr(uri, "http://") != NULL) {
        
        tok = strtok(uri, ":");
        strcpy(protocol, tok);
        tok = strtok(NULL, "\n");
        strcpy(uri, (tok + 2));
    }

    //find host and port and path in uri
    if(strstr(uri, ":") != NULL) { //specified host and port number
        tok = strtok(uri, ":");
        strcpy(host, tok);
        tok = strtok(NULL, "/");
        strcpy(port, tok);
        tok = strtok(NULL, "\n");
        if(tok != NULL){
            strcpy(path, tok);
        }
    } else {
        tok = strtok(uri, "/");
        strcpy(host, tok);
        if(strcmp(host, "localhost") == 0 || strcmp(host, "127.0.0.1") == 0){// If port not specified but localhost is used, assume that it is reffering to the tiny server port, otherwise default to port 80 as requried
            strcpy(port,"4500");
        } else{
            strcpy(port,"80");
        }
        tok = strtok(NULL, "\n");
        
        if(tok != NULL){
            strcpy(path, tok);
        }
        
    }
    printf("Protocol: %s\tHost: %s\tPort: %s\tPath: %s\n",protocol,host,port,path);

    clientfd = Open_clientfd(host, port);
    if(clientfd < 0) {//Calling read to receive bytes from a socket that has been prematurely closed will cause read to return -1 with errno set to ECONNRESET.
    //this ensures the proxy does not terminate due to this error.
        printf("Invalid host or port number");
        return;
    }
    
    memset(buf, '\0', sizeof(buf));

    strcat(buf, method);//Build buffer to send to the server 
    if(path[0] != '\0') {
        strcat(buf, " /");
        strcat(buf, path);
    }
    strcat(buf, " HTTP/1.0\r\nHost: ");
    strcat(buf, host);
    strcat(buf, ":");
    strcat(buf, port);
    strcat(buf, "\r\nUser-Agent: ");
    strcat(buf, user_agent_hdr);
    strcat(buf, "Connection: close\r\nProxy-Connection: close\r\nAccept-Language: en-US,en;q=0.5\n\rAccept-Encoding: gzip, deflate, br\n\r\n");
    
    printf("Client To Proxy To Server: %s\n", buf);
    Rio_writen(clientfd, buf, strlen(buf));//send to server

    Rio_readinitb(&rio, clientfd);
    while(Rio_readlineb(&rio, buf, MAXLINE) > 0) {//recieve data from server and send to client
        printf("Server To Proxy To Client: %s\n", buf);
        Rio_writen(fd, buf, strlen(buf));
    }
    Close(clientfd);
}