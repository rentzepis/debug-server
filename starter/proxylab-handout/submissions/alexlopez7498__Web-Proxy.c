#include <stdio.h>
#include <string.h>

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static char *FireFox = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void parse_uri(char *uri, char *hostname, int *port, char *pathORquery) 
{
    char *index = strstr(uri,"//"); // we use strstr to go to the // in the uri
    *port = 80;   // we set the port to 80 since thats the default for it

    if (index != NULL) // strstr gave us // then we just add two so that it can to the hostname
    {
        index = index + 2;
    } 
    else if (index == NULL) // strstr returns NULL if the string was not found
    {
        index = uri;
    }

    for (;*index != ':' && *index != '/';index++,hostname++)  // we iterate through and check if its : or / and increment the index and hostname
    {
        *hostname = *index; // copy it into the hostname
    }
    *hostname = '\0';  // then we end it 

    if (*index == ':') // if we get to a : then we use sscanf
    {
        sscanf(index + 1, "%d%s", port, pathORquery);
    } 
    else if(*index != ':') // if were not at a : then we use strcpy
    {
        strcpy(pathORquery, index);
    }
}

int connectingServer(int port, char* hostname) 
{
    int server;
    char portString[MAXLINE];
    sprintf(portString,"%d",port); // get to the port to a string
    server = Open_clientfd(hostname,portString); // then use open clientfd
    return server;
}

void addHeaders(char *serverHeaders, int hostChecker, int userAgentChecker, char *hostname, char *FireFox) 
{
    char hostHeader[MAXLINE] = "";

    if (hostChecker == 0)  // we check if the host header was not set by existing headers
    {
        sprintf(hostHeader, "Host: %s\r\n", hostname);
        strcat(serverHeaders, hostHeader);
    }
    if (userAgentChecker == 0) 
    {
        strcat(serverHeaders, FireFox); // we check if the agent header was not set by existing headers
    }
    strcat(serverHeaders, "\r\n"); // then we add the ending to it
}

void headerMaker (rio_t rio, char *serverHeaders, char *hostname, char *pathORquery) 
{
    char buffer[MAXLINE]; 
    int hostChecker = 0;
    int userAgentChecker = 0;

    sprintf(serverHeaders, "GET %s HTTP/1.0\r\n", pathORquery); // we get the main string
    strcat(serverHeaders, "Connection: close\r\n"); // we add the connection and the proxy connection
    strcat(serverHeaders, "Proxy-Connection: close\r\n"); 

    if (strncmp(rio.rio_bufptr, "",strlen("")) != 0) 
    {
        for (;Rio_readlineb(&rio, buffer, MAXLINE) != 0;) // go through all the server headers
        {
            if (strcmp(buffer, "\r\n") == 0) // if we reach the end we break the loop
            {
                break;
            } 
            else if (strncmp(buffer, "Host: ", strlen("Host: ")) == 0) 
            {
                hostChecker = 1;
                strcat(serverHeaders, buffer); // we add the host
            } 
            else if (strncmp(buffer, "User-Agent: ", strlen("User-Agent: ")) == 0) 
            {
                userAgentChecker = 1;
                strcat(serverHeaders, buffer); // we add the user-agent
            } 
            else if (!strncmp(buffer, "User-Agent: ", strlen("User-Agent: ")) == 0 || strncmp(buffer, "Connection: ", strlen("Connection: ")) == 0 || strncmp(buffer, "Proxy-Connection: ", strlen("Proxy-Connection: ")) == 0) 
            {
                strcat(serverHeaders, buffer); // other headers that can be added to the server headers
            }
        }
    }

    addHeaders(serverHeaders, hostChecker, userAgentChecker, hostname, FireFox); // we go into this function to check if we went into the host and user agent and other headers
}

void doitRequest(int clientfd, rio_t rio, char *uri) 
{
    int openingServer;
    char hostname[MAXLINE];
    char pathORquery[MAXLINE];
    char serverHeaders[MAXLINE] = "";
    char res[MAXLINE] = "";
    int port;
    size_t len;

    parse_uri(uri, hostname, &port, pathORquery); // first we parse the uri
    openingServer = connectingServer(port,hostname); // we oepn the connection

    if (openingServer < 0) // check to see if the connection is open
    {
        return;
    }

    headerMaker(rio, serverHeaders, hostname, pathORquery); // we go into a heaer maker function so that it can be made

    Rio_readinitb(&rio, openingServer);
    Rio_writen(openingServer, serverHeaders, strlen(serverHeaders)); // now here we are writing to the socket of theserver

    for(;(len = Rio_readlineb(&rio, res, MAXLINE)) != 0;) // here we read the repoonses of the server
    {
        Rio_writen(clientfd, res, len); 
    }

    Close(openingServer);
}

void doit(int clientfd) // this doit function was similar to the tiny.c that was given
{
    char buffer[MAXLINE];
    char method[8];
    char uri[MAXLINE];
    char version[8];
    rio_t rio;

    Rio_readinitb(&rio, clientfd);

    if (!Rio_readlineb(&rio, buffer, MAXLINE)) // this gets the http request
    {
        return;
    }
    printf("%s", buffer);
    sscanf(buffer, "%s %s %s", method, uri, version); //line:netp:doit:parserequest

    // check to see if its a GET
    if (strcasecmp(method, "GET") != 0) 
    {
        return;
    }

    doitRequest(clientfd, rio, uri); // we call the doitRequest functoin so that it can parse and open the server connection
}

int main(int argc, char **argv) // main was from one of lecture codes
{
    int listenfd, connfd;
    char hostname[MAXLINE];
    char port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if (argc != 2) 
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    if ((listenfd = open_listenfd(argv[1])) < 0) 
    {
        printf("Unable to open port %s\n", argv[1]);
        return 1;
    }

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
            port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);
        Close(connfd);
    }

    return 0;
}