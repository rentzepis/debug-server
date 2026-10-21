#include <stdio.h>
#include "csapp.h"

// debug macro
#define DEBUG 1
#if DEBUG
    #define DBG(...) printf(__VA_ARGS__)
#else
    #define DBG(...)
#endif

void request_handle(int fd);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void build_request(rio_t *rp, char *request, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    // check command line args
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        request_handle(connfd);
        Close(connfd);
    }
    return 0;
}

// handle one HTTP request/response transaction
void request_handle(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    char request[MAXLINE];
    rio_t rio_client, rio_server;
    int serverfd;

    // read request line from client
    Rio_readinitb(&rio_client, fd);
    if (!Rio_readlineb(&rio_client, buf, MAXLINE)) {
        return;
    }
    printf("Request: %s", buf);

    // parse request line into method, uri, and version
    sscanf(buf, "%s %s %s", method, uri, version);

    // only support GET method
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented", "Proxy does not implement this method");
        return;
    }

    // parse URI to get hostname, port, and path
    parse_uri(uri, hostname, port, path);
    
    // build HTTP request to send to server
    build_request(&rio_client, request, hostname, port, path);

    // connect to the server
    serverfd = Open_clientfd(hostname, port);
    if (serverfd < 0) {
        clienterror(fd, hostname, "502", "Bad Gateway",
                    "Failed to connect to server");
        return;
    }

    // send request to server
    Rio_writen(serverfd, request, strlen(request));

    // read response from server and forward to client
    Rio_readinitb(&rio_server, serverfd);
    ssize_t n;
    while ((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, n);
    }

    Close(serverfd);
}

// parse URI into hostname, port, and path
// URI format: http://hostname:port/path or http://hostname/path
void parse_uri(char *uri, char *hostname, char *port, char *path)
{
    char *ptr;
    char uri_copy[MAXLINE];
    
    strcpy(uri_copy, uri);
    
    // default HTTP port (80)
    strcpy(port, "80");
    
    // skip over "http://"
    ptr = strstr(uri_copy, "//");
    if (ptr != NULL) {
        ptr += 2;
    }
    else {
        ptr = uri_copy;
    }
    
    // extract hostname and path
    char *path_start = strchr(ptr, '/');
    if (path_start != NULL) {
        *path_start = '\0';             // temporarily terminate hostname:port string
        strcpy(path, path_start + 1);   // save path (after '/')
        *(path_start) = '/';            // restore the '/'
        
        // prepend '/' to path if needed
        char temp[MAXLINE];
        strcpy(temp, path);
        sprintf(path, "/%s", temp);
    }
    else {
        strcpy(path, "/");
    }
    
    // check for port number in hostname
    char *port_start = strchr(ptr, ':');
    if (port_start != NULL && (path_start == NULL || port_start < path_start)) {
        *port_start = '\0';  // terminate hostname
        strcpy(hostname, ptr);
        strcpy(port, port_start + 1);
        
        // remove path from port if it got included
        char *slash_in_port = strchr(port, '/');
        if (slash_in_port != NULL) {
            *slash_in_port = '\0';
        }
    }
    else {
        // no port specified
        if (path_start != NULL) {
            *path_start = '\0';
            strcpy(hostname, ptr);
            *path_start = '/';
        }
        else {
            strcpy(hostname, ptr);
        }
    }
}

// Builds the HTTP request to send to the server
void build_request(rio_t *rp, char *request, char *hostname, char *port, char *path)
{
    char buf[MAXLINE];

    // all lines in an HTTP requests must end with \r\n
    // every HTTP request must end with a blank line
    
    // build request line (only GET is supported)
    sprintf(request, "GET %s HTTP/1.0\r\n", path);
    
    // add host header
    sprintf(buf, "Host: %s\r\n", hostname);
    strcat(request, buf);
    
    // add the given User-Agent header
    strcat(request, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n");
    
    // always adding the following Connection and Proxy-Connection headers
    strcat(request, "Connection: close\r\n");
    strcat(request, "Proxy-Connection: close\r\n");
    
    // read and filter client headers
    while (Rio_readlineb(rp, buf, MAXLINE) > 0)
    {
        if (strcmp(buf, "\r\n") == 0) {
            break;
        }
        
        // adds any headers except Host, User-Agent, Connection, Proxy-Connection
        if (strncasecmp(buf, "Host:", 5) != 0 &&
            strncasecmp(buf, "User-Agent:", 11) != 0 &&
            strncasecmp(buf, "Connection:", 11) != 0 &&
            strncasecmp(buf, "Proxy-Connection:", 17) != 0) {
            strcat(request, buf);
        }
    }
    
    // add required blank line
    strcat(request, "\r\n");

    // methods like POST and PUT would have a body, but we only handle GET

    // print final request for debugging
    DBG("Final Request:\n%s", request);
}

// Returns error message to client
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    // builds HTML error page
    sprintf(body, "<html><title>Proxy Error</title>");
    strcat(body, "<body bgcolor=\"ffffff\">\r\n");
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    strcat(body, buf);
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    strcat(body, buf);
    strcat(body, "<hr><em>The Proxy Web server</em>\r\n");

    // prints HTTP response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
