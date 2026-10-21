/* THI NGUYEN CS475 F2017*/
#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connectionHeader = "Connection: close\r\n";
static const char *proxyHeader = "Proxy-Connection: close\r\n";
static const char *hostHeader = "Host: %s\r\n";
static const char *endHeader = "\r\n";
void *thread(void *vargp);

void doit(int connfd);
void parse_uri(char *uri,char *hostname,char *path,int *port);
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg);
int debug = 1;
int main(int argc,char **argv)
{
    int listenfd, *connfd;
    char hostname[MAXLINE],port[MAXLINE];
    socklen_t  clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    
    /* Check command line args */
    if(argc != 2){
        fprintf(stderr,"Usage :%s <port> \n",argv[0]);
        exit(1);
    }

    /* The open_listenfd function returns a listening descriptor 
        that is ready to receive connection requests on port argv[1].*/
    listenfd = Open_listenfd(argv[1]);
    while(1){
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int));
        *connfd = Accept(listenfd,(SA *)&clientaddr,&clientlen);

        /*print accepted message*/
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,
                    port,MAXLINE,0);
        printf("Accepted connection from (%s %s).\n",hostname,port);
        Pthread_create(&tid,NULL,thread,connfd);
    }
    return 0;
}

void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*handle the client HTTP transaction */
void doit(int connfd)
{
    printf("In doit \n");
    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char httpHdrToServer [MAXLINE];
    /*store the request line arguments*/
    char hostname[MAXLINE],path[MAXLINE];
    int port, serverfd; /*the end server file descriptor*/

    rio_t client_rio,server_rio;

    Rio_readinitb(&client_rio,connfd);
    Rio_readlineb(&client_rio,buf,MAXLINE);

    /*
        At this point, if the request header from client in the right format,
        the buf should have this similar structure:
        GET http://cs.gmu.edu(:port)/contact/index.html HTTP/1.1
    */

    /* parse the method, uri, version from buf after reading the client request line */
    sscanf(buf,"%s %s %s",method,uri,version); 
    /*
        method = GET, uri = http://cs.gmu.edu/contact/index.html , version = HTTP/1.1
    */ 

    /* If method is not GET, send error message to client */
    if(strcasecmp(method,"GET")){
        clienterror(connfd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }

    /* parse the uri to get hostname, file path, and port */ 
    parse_uri(uri,hostname,path,&port);

    /* Build the HTTP header which will send to the end server*/
    char tempBuf[MAXLINE],requestHdr[MAXLINE],other_hdr[MAXLINE],host_hdr[MAXLINE];
    /* Make request line "GET (path) HTTP/1.0\r\n" */
    sprintf(requestHdr,"GET %s HTTP/1.0\r\n",path);

    /* Read additional request headers for client rio and modify it when needed */
    while(Rio_readlineb(&client_rio,tempBuf,MAXLINE)>0) {
        if(strcmp(tempBuf,"\r\n") == 0) break;/*EOF*/
        if(!strncasecmp(tempBuf,"Host",strlen("Host"))) {
            strcpy(host_hdr,tempBuf);
            continue;
        }
        if(!strncasecmp(tempBuf,"Connection",strlen("Connection"))
            &&!strncasecmp(tempBuf,"Proxy-Connection",strlen("Proxy-Connection"))
            &&!strncasecmp(tempBuf,"User-Agent",strlen("User-Agent")))
        { strcat(other_hdr,tempBuf); }
    }
    if(strlen(host_hdr)==0)
    {
        sprintf(host_hdr,hostHeader,hostname);
    }
    sprintf(httpHdrToServer,"%s%s%s%s%s%s%s",requestHdr, host_hdr, connectionHeader, proxyHeader,
                        user_agent_hdr, other_hdr, endHeader);

    /* finish building HTTP header */


    /*connect to the end server*/
    char portNumber[20];
    sprintf(portNumber,"%d",port);

    /* The open_clientfd function establishes a connection with a server running on
        host hostname and listening for connection reqests on port number port */
    printf("here\n");
    serverfd = Open_clientfd(hostname,portNumber);
    if(serverfd<0){
        printf("connection error\n");
        return;
    }

    Rio_readinitb(&server_rio,serverfd);
    /*write the http header to endserver*/
    Rio_writen(serverfd,httpHdrToServer,strlen(httpHdrToServer));

    /*receive message from end server and send to the client*/
    size_t n;
    while((n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0)
    {
        Rio_writen(connfd,buf,n);
    }
    Close(serverfd);
}




/* parse the uri with this format http://cs.gmu.edu/contact/index.html
        If sucessully, 
        hostname: cs.gmu.edu
        file path: /contact/index.html

*/

void parse_uri(char *uri,char *hostname,char *path,int *port)
{
    // Ex URI:  http://cs.gmu.edu:8080/contact/index.html 
    *port = 80; // default port if the port optional field is not provided
    /* 
        doubleSlashPos = the pointer to the first occurrence of // in uri 
        if doubleSlashPos = NULL, // is not found
    */
    char* doubleSlashPos = strstr(uri,"//");
    if (doubleSlashPos) {
        // if there is //, then get the first postion of cs.gmu.edu:8080/contact/index.html 
        doubleSlashPos = doubleSlashPos+2;
    }
    else {
        doubleSlashPos = uri;
    }
    /*  
        get the port in case of the uri in this format 
        http://cs.gmu.edu:8080/contact/index.html 
        search for the first occurence of : in string after doubleSlashPos
     */
    char* portOrPathPos = strstr(doubleSlashPos,":");

    // if uri includes port 
    if(portOrPathPos!=NULL) {
        *portOrPathPos = '\0';
        sscanf(doubleSlashPos,"%s",hostname); // E.x: get hostname = cs.gmu.edu from cs.gmu.edu:8080/contact/index.html 
        sscanf(portOrPathPos+1,"%d%s",port,path); // E.x: get port = 8080, path = /contact/index.html 
    }
    else {
        // if uri doesn't have port but have path 
        portOrPathPos = strstr(doubleSlashPos,"/");
        if(portOrPathPos!=NULL) {
            *portOrPathPos = '\0';
            sscanf(portOrPathPos,"%s",hostname);
            *portOrPathPos = '/';
            sscanf(portOrPathPos,"%s",path);
        }
        else {
            // if uri just have hostname
            sscanf(portOrPathPos,"%s",hostname);
        }
    }
    if (debug) {
        printf("hostname: %s\n", hostname);
        printf("path: %s\n", path);
        printf("port: %s\n", port);
    }
    return;
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */











