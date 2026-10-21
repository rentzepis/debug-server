#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";

// Function Prototypes:
void build_http_header(char *buf, char *path);
void forward_request(int connfd, char *host, char* port, char *path, rio_t rio);
void parce_uri(char *uri, char *host, char *port, char *path);
void perform_proxy_request(int connfd);

/*************************************** Helper Functions **********************************************/
/* This function will build the HTTP header */
// *********** TODO: Try to make more robust later
void build_http_header(char* buf, char* path ) {
    strcpy(buf, "GET ");
    strcat(buf, path);
    strcat(buf, " HTTP/1.0\r\n"); // HTTP/1.0 is the only version supported
    strcat(buf, user_agent_hdr);
    strcat(buf, connection_hdr);
    printf("Request to be forwarded to the server: \n%s\n", buf);
}

/* This function will fordward the request to the server */
void forward_request(int connfd, char* host, char* port, char *path, rio_t rio) {
    char buf[MAXLINE], temp[] = "\r\n";
    int clientfd;
    rio_t server_rio;

    //Debugging print statements
    printf("Host = %s\n", host);
    printf("Port = %s\n", port);
    printf("Path = %s\n", path);

    // Open the connection to the server
    if((clientfd = Open_clientfd(host, port)) < 0) {
        printf("Error: Failed to open connection to server\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return;
    }
    printf("Opened connection to server\n");

    // Build the HTTP request header
    build_http_header(buf, path);

    // Write the HTTP request header to the server
    Rio_readinitb(&server_rio, clientfd);
    Rio_writen(clientfd, buf, strlen(buf));
    Rio_writen(clientfd, temp, strlen(temp));

    // Write back to client
    while(1){
        if(Rio_readlineb(&server_rio, buf, MAXLINE) == 0) {
            //printf("Buffer: %s\n", buf);
            break;
        }
        Rio_writen(connfd, buf, strlen(buf));
    }

    // Close the server connection
    Close(clientfd);
    return;
}
/* This function will parse the uri into host, port, and path 
    It takes created pointers and fills them with correct values*/
void parce_uri(char *uri, char *host, char *port, char *path) {
    char *pos = uri; // String tracker
    int secure = 0;

    // Check if it starts with http://
    if (strstr(uri, "http://") != NULL) {
        pos = uri + 7;  // Move past "http://"
    }

    if (strstr(uri, "https://") != NULL) {
        pos = uri + 8;  // Move past "https://"
        secure = 1;
    }

    // Extract the host
    while (*pos != ':' && *pos != '/' && *pos != '\0') {
        *host = *pos;
        host++; 
        pos++;    
    }
    *host = '\0'; // Null terminate the host string

    // Extract the port if present
    if (*pos == ':') {
        pos++; // Skip the ':'
        while (*pos != '/' && *pos != '\0') {
            *port = *pos;
            port++;  
            pos++; 
        }
        *port = '\0';
    }
    else if (secure) {
        strcpy(port, "443");
    } else {
        strcpy(port, "80");
    }

    // Extract the path if present
    if (*pos == '/') {
        while (*pos != '\0') {
            *path = *pos;
            path++;
            pos++;
        }
    }
    *path = '\0';

    // Debugging output, enable if you need to troubleshoot program in future
    /*
    printf("In URI decider, Host = %s\n", host);
    printf("In URI decider, Path = %s\n", path);
    printf("In URI decider, Port = %s\n", port);   
    printf("In URI decider, URI = %s\n", uri);
    */
    return;
}

/* This function will perform proxy request
It takes the request, reads the information, strips it, and passess values for forwarding */
// Referenced tiny.c for help with reading connection info
void perform_proxy_request(int connfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t rio;

    // Initialize the rio buffer
    Rio_readinitb(&rio, connfd);
    memset(rio.rio_buf, 0, 8192);  // Clear the buffer (Rio doesn't automatically clear the buffer)

    if(!Rio_readlineb(&rio, buf, MAXLINE)) {
        printf("Failing here\n");
        printf("Error: Rio_readlineb failed\n");
        return;
    }
    printf("Proxy received request: \n%s\n", buf);
    sscanf(buf, "%s %s %s", method, uri, version); // scanf reads formatted input from a string 

    // TODO: Implement other hhtp versions and https

    // Check if the method is GET, otherwise unsupported
    if (strcasecmp(method, "GET")) {
        fprintf(stderr, "Proxy received method other than GET and does not support it\n");
        return;
    }

    // TODO: Implement other http methods (POST, PUT, DELETE, etc.)

    // Parse the uri into host, port, and path
    char host[MAXLINE], path[MAXLINE], port[MAXLINE];
    parce_uri(uri, host, port, path);
    
    // Forward the request to server
    forward_request(connfd, host, port, path, rio);
    return;
}

/*************************************** Main Function **********************************************/
/* Main will run the proxy indefinently */ 
// Referenced Network Programming Pt.1 CS360 Class slides for main routine
// Furthure referenced tinyserver code for socket operations & techniques
int main(int argc, char **argv)
{
    char host[MAXLINE]; //maxline defined in csapp.h, maximum input line length
    char port[MAXLINE]; 
    struct sockaddr_in clientaddr;
    socklen_t clientlen;
    int listenfd, connfd; //listenfd = listening file descriptor, connfd = connection file descriptor

    if (argc != 2) { // Check user input
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    listenfd = Open_listenfd(argv[1]); //open listening file descriptor (socket) on user-input port
    if(listenfd < 0) {
        printf("Error: Failed to open listening socket\n");
        fprintf(stderr, "Error: %s\n", strerror(errno));
        return 1;
    }

    // Run the proxy server indefinently
    while(1) {
        clientlen = sizeof(clientaddr); // Get the size of the client address

        // Wait and listen for a connection
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //accept connection
        if(connfd < 0) {
            printf("Error: Failed to accept connection\n");
            fprintf(stderr, "Error: %s\n", strerror(errno));
            return 1;
        }

        // Convert the client address to a string
        Getnameinfo((SA *) &clientaddr, clientlen, host, MAXLINE, port, MAXLINE, 0); //get name info

        // Print the client address and perform proxy request
        printf("Accepted connection from (%s, %s)\n", host, port);
        perform_proxy_request(connfd);
        Close(connfd); //close connection
    }
    printf("%s", user_agent_hdr);
    return 0;
}