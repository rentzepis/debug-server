/*
 * Shaofeng Qin, shaofenq
 shaofenq@cmu.edu

In this lab, we have three parts, and the checkpoint only requires the first part that implement a sequential server(proxy). 
the basic implementation skeleton is based on the tiny server and textbook
 */

/* Some useful includes to help you get started */

#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1\r\n";
//Always send the following Connection header:
static const char *header_connection = "Connection: close\r\n";
//Always send the following Proxy-Connection header:
static const char *header_proxy = "Proxy-Connection: close\r\n";

// functions used
void doit(int fd);
void parse_uri(char *uri, char *hostname, int* port, char *path);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void generate_header(char *header, char* hostname, int* port, char* path, rio_t* rio_client);



int main(int argc, char **argv) 
{
    //printf("%s", header_user_agent);
    int listenfd, connfd, port, clientlen;
    char hostname[MAXLINE], port[MAXLINE];
    struct sockaddr_in clientaddr;
    /* Check command line args */
    if (argc != 2) 
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    listenfd = open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Connection is established from host: %s, port: %s\n", hostname, port);
        doit(connfd);
        close(connfd);
    }
    return 0;
}

//skeleton based on textbook, but made some modification since it does the job of a proxy(
// send/receive mesaages on both ends: client and server
/* this has some difference with server since we only need to transfer messages
instead of dealing with staic or dynamic requests.
*/
void doit(int fd)
{ 
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE], path[MAXLINE];
    char header[MAXLINE];
    rio_t rio_client, rio_server;
    int server_fd, message_size;
    int *port = NULL;
    /* step 1: Read request line and headers */
    rio_readinitb(&rio_client, fd);
    rio_readlineb(&rio_client, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    // we are required to only handle the GET request for now, otherwise, print not implememnted
    if (strcasecmp(method, "GET")) 
    {
        clienterror(fd, method, "501", "Not Implemented","Proxy does not implement this method");
        return;
    }
    
    /* step 2: forward request to server
    Parse URI from GET request
    and save in the varibales: port, hostname, path
    port is by default to be 80 */
    parse_uri(uri, hostname, port, path);

    // build the request header sent to server
    generate_header(header, hostname, port, path, &rio_client);

    //make connection to server
    /* note port is in type int*, we need to type casting to char*
    */
   char port_buf[20];
   sprintf(port_buf, "%d", *port);
   if ((server_fd = open_clientfd(hostname, port_buf)) <0)
   {
        //error message
        printf("connection to server failed.\n");
        return;
    }
    //forward the header we generate to the server
    rio_readinitb(&rio_server, server_fd);
    rio_writen(server_fd, header, strlen(header));
    
    
    //step3 & 4read from server's reply and forward to client
    while ((message_size = rio_readlineb(&rio_server, buf, MAXLINE))>0)
    {
        rio_writen(fd, buf, message_size);
    }
    
    close(server_fd);

}

/*parse the client' request
pre: full uri
post: hostname, server port, path
by default, the port is 80 if not specified

uri -> http://www.cmu.edu:8080/hub/index.html 
ptr1 -> www.cmu.edu:8080
ptr2 -> /hub/index.html

strstr is the function that finds the first occurance of the specified string
*/
void parse_uri(char *uri, char *hostname, int* port, char *path)
{
    char *hostname = "www.cmu.edu";
    *port = 80;
    char *start, *port_pos, *path_pos;
    if (start = strstr(uri, "//") == NULL)
    {
        start = uri;
    }
    else{
        start = start + 2; //get to www
    }
    //get the port position using:
    if ((port_pos = strstr(start, ":")) == NULL)
    {
        //port will be "80" by default
        if ((path_pos = strstr(start, "/") != NULL))
        {
            *path_pos = '\0'; //add termination
            //extract hostname
            sscanf(start, "%s", hostname);
            //extract path only, port has been defaulted to 80
            *path_pos = '/'; //restore the character from termination null character
            sscanf(path_pos, "%s", path);
        }
        else
        {
            // if path_pos is NULL, no file path, only extract file path
            sscanf(start, "%s", hostname);
        }
    }
    //port_pos is not NULL
    else
    {
        // add termination
        *port_pos = '\0';
        //extract hostname
        sscanf(start, "%s", hostname);
        //extract port and path
        sscanf(port_pos + 1, "%d%s", port, path);

    }
    
    
}



void generate_header(char *header, char* hostname, int* port, char* path, rio_t* rio_client)
{
    char buf[MAXLINE]; // create a buf for reading client's request headers
    /*check host header and get other request header for client rio then change it */
    sprintf(header, "GET %s HTTP/1.0\r\n", path);
    while(rio_readlineb(rio_client, buf, MAXLINE) >0){
        if(strcmp(buf, "\r\n") ==0)
        {
            break;
        }
        /* if a client sends any additional request headers as part of an HTTP request, your proxy 
        should forward them unchanged.*/
        if (strstr(buf,"Host:") != NULL)
        {
            continue; 
        }
        if (strstr(buf,"User-Agent:") != NULL)
        {
            continue; 
        }
        if (strstr(buf,"Connection:") != NULL) 
        {
            continue;
        }
        if (strstr(buf,"Proxy-Connection:") != NULL)
        {
            continue;
        }
        //write into header without any change to them
        sprintf(header,"%s%s", header,buf);
    }
    /*format the header by making:
    first line: GET + path
    second line: Host: hostname  + port
    third line:  User-Agent: Mozilla/5.0 (X11; Linux x86 64; rv:3.10.0) Gecko/20191101 Firefox/63.01\r\n
    forth line: Connection: close\r\n
    fifth line: Proxy-Connection: close\r\n
    sixth line: \r\n  ->blank line
    */
    
    sprintf(header, "%sHost: %s:%d\r\n",header, hostname,*port);
    sprintf(header, "%s%s%s%s", header, header_user_agent,header_connection,header_proxy);
    sprintf(header, "%s\r\n", header);
}


/* from text book
 * return error message to client
 */
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}

