#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */
void doit(int fd, int ascfd);

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor="
                  "ffffff"
                  ">\r\n",
            body);
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
//end client error

void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while (strcmp(buf, "\r\n"))
    { //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}
//end read_requesthdr

int parse_uri(char *uri, char *rshost,char *rsport, char *filename, char *cgiargs)
{
    char *ptr;

    if (!strstr(uri, "cgi-bin"))
    { /* Static content */   //line:netp:parseuri:isstatic
        strcpy(cgiargs, ""); //line:netp:parseuri:clearcgi
                                  //line:netp:parseuri:beginconvert1
        ptr = uri+7;
        while (*(ptr) != ':')
            *(rshost++)=*(ptr++) ; 
        ++ptr; 
        while(*(ptr) !='/')
            *(rsport++)=*(ptr++);
                                    //line:netp:parseuri:endconvert1
        if (uri[strlen(uri) - 1] == '/'){    //line:netp:parseuri:slashcheck
            strcpy(filename, "/"); //line:netp:parseuri:appenddefault
            return 1;
        }
        while (*ptr)
        {
            *(filename++) = *(ptr++);
        }
    }
    else
    { /* Dynamic content */    //line:netp:parseuri:isdynamic
        ptr = index(uri, '?'); //line:netp:parseuri:beginextract
        if (ptr)
        {
            strcpy(cgiargs, ptr + 1);
            *ptr = '\0';
        }
        else
            strcpy(cgiargs, ""); //line:netp:parseuri:endextract
        strcpy(filename, ".");   //line:netp:parseuri:beginconvert2
        strcat(filename, uri);   //line:netp:parseuri:endconvert2
        return 0;
    }
}
/* $end parse_uri */

char *realserver = "iuya.ml";
char *rsport = "5050";
char *proxyserver = "localhost";
char *psport = "1081";
int main(int argc, char **argv)
{
    int listenfd, connfd;
    int ascfd;
    char hostname[MAXLINE], port[MAXLINE]; //proxy client host and port
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    listenfd = Open_listenfd(psport);
    while (1)
    {
        printf("listening ****\n");
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        //only accept localhost
       
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd, ascfd);
    }
    return 0;
}

void doit(int fd, int ascfd)
{
    int filesize=0;
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char rshost[MAXLINE],rsport[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;
    rio_t riors;
    rio_readinitb(&rio, fd);

    if (!Rio_readlineb(&rio, buf, MAXLINE)) //line:netp:doit:readrequest
        return;
    if(strstr(buf,"iuya")<0){
        printf("**not the host\n");
        return;
    }
    printf("**from client:%s\n", buf);

    sscanf(buf, "%s %s %s", method, uri, version);
    is_static = parse_uri(uri, rshost, rsport,filename, cgiargs);
    printf("***uri:%s filename:%s cgiargs:%s ****\n", uri, filename, cgiargs);
    if (is_static)
    {
    }
    ascfd = Open_clientfd(rshost, rsport);
    Rio_readinitb(&riors,ascfd);
    //request real server
    if(strcmp(filename,"index.html")==0){
        filename[0]='/';
        filename[1]='\0';
    }
    sprintf(buf, "GET %s %s\r\n", filename, version);
    //todo need replace iuya.cf
    sprintf(buf, "%sHost: %s\r\n", buf,rshost);
    sprintf(buf, "%s\r\n", buf);
    printf("******reques sent from pserver:%s\n", buf);
    Rio_writen(ascfd, buf, strlen(buf));
    char response[2000] = {0};
    int n;
    while ( n=Rio_readlineb(&riors,buf,MAXLINE))
    {
        sprintf(response, "%s%s",response, buf);
        rio_writen(fd,buf,n);
        if(strstr(buf,"Length")){
            filesize=atoi(buf+16);
            Rio_readlineb(&riors,buf,MAXLINE);
            sprintf(response, "%s%s",response, buf);
            break;
        }
    }
    while(filesize>0){
        int n=Rio_readnb(&riors,buf,MAXLINE);
        Rio_writen(fd,buf,n);
        filesize-=n;
    }
    printf("\nproxy server have got web content***:\n%s\nenddd\n", response);
    
    printf("put in respose***\n");
    printf("writed to fd**\n");
    return;
}

// /* read request line and header */
//     if(!Rio_readlineb(&rio,buf,MAXLINE)){
//         return; }
//     sscanf(buf,"%s %s %s",method,uri,version);

// sprintf(buf,"HTTP/1.1 200 OK\r\n");
// sprintf(buf,"%sServer: myproxy\r\n",buf);
// sprintf(buf, "%sConnection: close\r\n", buf);
// sprintf(buf, "%sContent-length: %d\r\n", buf, 100);
// sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, "text/html");
// Rio_writen(fd, buf, strlen(buf));

int main1(int argc, char **argv)
{
    int clientfd;
    char *hostname, *port, buf[10000];
    if (argc != 3)
    {
        fprintf(stderr, "usage: %s host port\n", argv[0]);
        exit(0);
    }
    hostname = "localhost";
    port = "8080";

    clientfd = Open_clientfd(hostname, port);
    Rio_writen(clientfd, "GET / HTTP/1.1\r\nHost: www.taobao.com\r\n\r\n", 100);
    Rio_readn(clientfd, buf, 10000);
    printf("****\n%s\n ****", buf);
    return 0;
}