#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";


void doit(int fd);
int powerten(int i);
void read_requesthdrs(rio_t *rp);
void parseuri(char *uri, char *hostname, char *query, int *port);
void clienterror(int fd, char *cause, char *errnum, 
         char *shortmsg, char *longmsg);
static void request_header(char *buf, char *hostname, char *buf_version2);


int main(int argc, char **argv)
{
    //argv means the port defined by the client
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
    }

    
    //listenfd means the server port
    listenfd = Open_listenfd(argv[1]);
    while (1) {
    clientlen = sizeof(clientaddr);
    //connfd means the client port
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
    
    doit(connfd);                                             //line:netp:tiny:doit
    Close(connfd);                                            //line:netp:tiny:close
    }


    printf("%s%s%s", user_agent_hdr, accept_hdr, accept_encoding_hdr);

    return 0;
}

void doit(int fd) 
{
    
    int *port;
    port = malloc(sizeof(int));
    *port = 80;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXBUF], query[MAXBUF];
    char server_response[MAXLINE];
    rio_t rio, rio_ser;
    char buf_version2[MAXLINE];
    int serverfd;
    char cache_buf[MAX_OBJECT_SIZE];

    // initial the buffer with zero
    memset(buf, 0, sizeof(buf));
    memset(buf_version2, 0, sizeof(buf_version2));
    memset(method, 0, sizeof(method));
    memset(uri, 0, sizeof(uri));
    memset(version, 0, sizeof(method));
    memset(hostname, 0, sizeof(hostname));
    memset(query, 0, sizeof(query));
    memset(server_response, 0, sizeof(server_response));
    memset(cache_buf, 0, sizeof(cache_buf));
    /* obtain request from client and parse it in buf */
    rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("request from client: %s\n", buf);
    
    // parse request into method uri version
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    
    //check version
    if(!strcasecmp(version, "HTTP/1.1")){
        strcpy(version, "HTTP/1.0");
    }

    //check method
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs

    /* Parse URI from GET request */
    parseuri(uri, hostname, query, port);
    printf("content in query: %s\n", query);
    printf("content in hostname: %s\n", hostname);
    sprintf(buf_version2, "%s %s %s\r\n", method, query, version);
    printf("parse rui from get: %s\n", buf_version2);

    //request header
    request_header(buf, hostname, buf_version2);

    //connect proxy to server
    char port2[10];
    sprintf(port2, "%d", *port);
    serverfd = open_clientfd(hostname, port2);
    rio_readinitb(&rio_ser, serverfd);
    //send request to server;
    rio_writen(serverfd, buf_version2, strlen(buf_version2));
    //read data from server and store in cache_buf
    int len;
    len = rio_readnb(&rio_ser, server_response, 
                sizeof(server_response));
    printf("read data from server: %s\n", server_response);
    rio_writen(fd, server_response, len);

    // int count = 0;
    // while((len = rio_readnb(&rio_ser, server_response, 
    //             sizeof(server_response)))>0){
    //     rio_writen(fd, server_response, len);
    //     strcat(cache_buf, server_response);
    //     count++;
    //     memset(server_response, 0, sizeof(server_response));
    // }

    close(serverfd);



    
    
}

static void request_header(char *buf, char *hostname, char *buf_version2){
    // char str[] = "Host: ";
    // char * pch;
    // puts(str);
    // pch = strstr (buf,str);
    // if(pch!=NULL){
        strcat(buf_version2, "Host:");
        strcat(buf_version2, hostname);
        strcat(buf_version2, "\r\n");
    // }
    // char str1[] = "Accept:";
    // pch = strstr (buf,str1);
    // if(pch!=NULL){
        strcat(buf_version2, accept_hdr);
        strcat(buf_version2, "\r\n");
    // }
    // char str2[] = "Accept-Encoding:";
    // pch = strstr (buf,str2);
    // if(pch!=NULL){
        strcat(buf_version2, accept_encoding_hdr);
        strcat(buf_version2, "\r\n");
    // }
    // char str3[] = "User-Agent:";
    // pch = strstr (buf,str3);
    // if(pch!=NULL){
        strcat(buf_version2, user_agent_hdr);
        strcat(buf_version2, "\r\n");
    // }
    // char str4[] = "Proxy-Connection:";
    // pch = strstr (buf,str4);
    // if(pch!=NULL){
        strcat(buf_version2, "Proxy-Connection: close\r\n");
    // }
    // char str5[] = "Connection:";
    // pch = strstr (buf,str5);
    // if(pch!=NULL){
        strcat(buf_version2, "Connection: close\r\n");
    // }
}


void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    }
    return;
}

void parseuri(char *uri, char *hostname, char *query, int *port){
    int port_store[10];
    char port_num[2];
    int i,j;
    for(i=0; i<10; i++){
        port_store[i] = 0;
    }
    char *buf = uri;
    char *endbuf;
    endbuf = buf+strlen(buf);
    buf = buf+7;
    while(buf<endbuf){
        if(buf>=endbuf){
            strcpy(query, "");
            strcat(hostname, " ");
            break;
        }
        if(*buf == ':'){
            buf++;
            i = 0;
            *port=0;
            while(*buf!='/'){
                port_num[0]=*buf;
                port_num[1]='\0';
                port_store[i] = atoi(port_num);
                buf++;
                i++;
            }
            j=0;
            while(i>0){
                *port = *port + port_store[j]*powerten(i-1);
                j++;
                i--;
            }
        }
        if(*buf!='/'){
            sprintf(hostname, "%s%c", hostname, *buf);
        }
        else{
            strcat(hostname, "\0");
            strcpy(query, buf);
            break;
        }
        buf++;
    }
    
}
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

int powerten(int i){
    int temp = 1;
    while(i!=0){
        temp = temp*10;
        i--;
    }
    return temp;
}   
