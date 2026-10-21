#include <stdio.h>
#include <stdlib.h>
#include "csapp.h"
#include "cache.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE  1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum,  char *shortmsg, char *longmsg);
void *thread(void *vargp);
void buildrequesthdrs(char *hostname, char *buf, char *path, rio_t *rp);

int main(int argc, char **argv)
{
    int listenfd, *connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    /* Check command line args */
    if (argc != 2) 
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);
    cache_init();
    listenfd = Open_listenfd(argv[1]);
    
    while (1) 
    {
        printf("listening..\n");
        clientlen = sizeof(clientaddr);
        connfd = malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_create(&tid, NULL, thread, connfd);
        //free(connfd);
    }
}
/* Thread routine */
void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(Pthread_self());
    Free(vargp);
    doit(connfd);
    printf("close connfd\n");                                             
    Close(connfd);  
    return NULL;
}

void doit(int fd)
{

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], server_buf[MAXLINE],uriBackup[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    rio_t client_rio, server_rio;
    char port[10];
    /* Read request line and headers*/
    Rio_readinitb(&client_rio, fd);
    Rio_readlineb(&client_rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s",method, uri, version);
    strcpy(uriBackup, uri);
    if(strcasecmp(method, "GET"))
    {
        clienterror(fd, method, "501", "Not implemented", "Tiny does not implement this method");
        return;
    }


    /* Parse URI from GET request*/

    int index = cache_read(uri, fd);
    if(index >= 0)
    {
        printf("fetch cache\n");
        return;
    }

    parse_uri(uri, hostname, path, port);
    buildrequesthdrs(hostname, server_buf, path, &client_rio);
    int server_fd = Open_clientfd(hostname, port);
    
    Rio_readinitb(&server_rio,server_fd);
    if(server_fd < 0) printf("WRONG server_fd!\n");

     /* Build http request from proxy to server: GET /hub/index.html HTTP/1.0  */
    
    printf("BUILD PROXY REQUEST LINE AND HEADERS\n");

    /* Send request line and request headers to server */
    Rio_writen(server_fd, server_buf, strlen(server_buf)); 
    printf("SEND FROM PROXY TO SERVER\n");
    
    /* Read response from server */

    memset(buf, 0, MAXLINE);
    size_t n;
    int cacheSize = 0;
    int buf_len = strlen(buf);
    while((n = Rio_readlineb(&server_rio,server_buf,MAXLINE)) != 0)
    {
        if(buf_len > __UINT16_MAX__ - MAXLINE) break;
        if(cacheSize <= MAX_OBJECT_SIZE)
        {
            buf_len += sprintf(buf+buf_len,"%s",server_buf);
            cacheSize += n;
        }
        fprintf(stdout, "proxy recived %ld bytes\n", n);
        Rio_writen(fd, server_buf, n);
    }

    cache_write(buf, uriBackup, cacheSize);

    Close(server_fd);
}

void parse_uri(char *uri, char *hostname, char *path, char *port) 
{

    char* default_port = "80";
    char *ptr1 = strstr(uri, "//");
    if(ptr1) ptr1 += 2; 
    else ptr1 = uri;

    char *ptr2 = strstr(ptr1, ":");

    if(ptr2)/* www.cmu.edu:8080/hub/index.html */
    {
        strncpy(hostname, ptr1, (strlen(ptr1) - strlen(ptr2)));

        char *ptr3 = strstr(ptr2, "/");
        strcpy(path, ptr3);

        ptr2 += 1;
        
        strncpy(port,ptr2,(strlen(ptr2) - strlen(ptr3)));
    }
    else  /* www.cmu.edu/hub/index.html */
    {   
        char *ptr3 = strstr(ptr1, "/");
        strcpy(path, ptr3);
        strncpy(hostname, ptr1, (strlen(ptr1) - strlen(ptr3)));
        strcpy(port,default_port);
    }
}

void read_requesthdrs(rio_t *rp)
{
    char buf[MAXLINE];
    rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n"))
    {
        rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}

void buildrequesthdrs(char *hostname, char *buf, char *path, rio_t *rp)
{
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    int size = strlen(buf);
    size += sprintf(buf+size,"Host: %s\r\n", hostname);
    size += sprintf(buf+size, "%s", user_agent_hdr);
    size += sprintf(buf+size, "%s", connection_hdr);
    size += sprintf(buf+size, "%s", proxy_connection_hdr);

    char tmp[MAXLINE];
    Rio_readlineb(rp, tmp, MAXLINE);
    while(strcmp(tmp, "\r\n"))
    {   
        if((strstr(tmp,"Host:") != NULL)  || (strstr(tmp,"User-Agent:") != NULL)||(strstr(tmp,"Connection:") != NULL) || (strstr(tmp,"Proxy-Connection:") != NULL))
        {
            Rio_readlineb(rp, tmp, MAXLINE);
            continue;
        }
        size+=sprintf(buf+size,"%s",tmp);        
        Rio_readlineb(rp, tmp, MAXLINE);
    }
    size+=sprintf(buf+size,"\r\n"); 
}
