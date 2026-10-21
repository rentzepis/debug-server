#include <stdio.h>
#include "csapp.h"
#include <string.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void read_requesthdrs(rio_t *rp, char *dest, char *hostname);
int parse_uri(char *uri, char *hostname, char *port, char *path);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        if(Fork()==0){
            Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
            printf("Accepted connection from (%s, %s)\n", hostname, port);
            Close(listenfd);
            doit(connfd);
            Close(connfd);
            exit(0);
        }
        
        
        // doit(connfd);  //line:netp:tiny:doit
        // Close(connfd); //line:netp:tiny:close
        Close(connfd);
    }
}
/* $end tinymain */

/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd)
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE)) //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version); //line:netp:doit:parserequest
    if (strcasecmp(method, "GET"))
    { //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    } //line:netp:doit:endrequesterr
    char* headers[MAXLINE];

    int end_server_fd;
    char *port[6], hostname[MAXLINE], path[MAXLINE];
    char *request[MAXLINE];
    parse_uri(buf, hostname, port, path);
    end_server_fd = Open_clientfd(hostname, port);
    read_requesthdrs(&rio, headers, hostname);
    printf("%s\n", headers);

    rio_t end_server_rio;
    Rio_readinitb(&end_server_rio, end_server_fd);
    sprintf(request, "%s%s%s%s", "GET ", path, " HTTP/1.0\r\n", headers);
    //Rio_writen(end_server_fd, "GET ", 4);
    //Rio_writen(end_server_fd, path, strlen(path));
    //Rio_writen(end_server_fd, " HTTP/1.0\r\n\r\n", strlen(" HTTP/1.0\r\n\r\n"));
    Rio_writen(end_server_fd, request, strlen(request));
    size_t n;

    while ((n = Rio_readlineb(&end_server_rio, buf, MAXLINE)) != 0)
    {
        Rio_writen(fd, buf, n);
    }
    // Rio_readlineb(&end_server_rio, buf, MAXLINE);
    // rio_readn(&end_server_rio, buf, MAXLINE);
    // printf("%s\n", buf);
    Close(end_server_fd);
}
/* $end doit */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp, char *dest, char *hostname)
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    // printf("%s", buf);
    int is_host = 0;
    char *host = "Host: ";
    int is_ua = 0;
    char *user_agent = "User-Agent: ";
    int is_connection = 0;
    char *connection = "Connection: ";
    int is_proxy_connection = 0;
    char *proxy_connection = "Proxy-Connection: ";
    char *headers[MAXLINE]={0};
    char *headers_pointer=headers;
    while (strcmp(buf, "\r\n"))
    { //line:netp:readhdrs:checkterm
        if (strstr(buf, host))
        {
            strncpy(headers_pointer, buf, strlen(buf));
            headers_pointer+=strlen(buf);
            is_host = 1;
        }
        else if (strstr(buf, user_agent))
        {
            strncpy(headers_pointer, buf, strlen(buf));
            headers_pointer+=strlen(buf);
            is_ua = 1;
        }
        else if (strstr(buf, connection))
        {
            // strncpy(headers, buf, strlen(buf));
            // is_host = 1;
        }
        else if (strstr(buf, proxy_connection))
        {
            // strncpy(headers, buf, strlen(buf));
            // is_host = 1;
        }
        else
        {
            strncpy(headers_pointer, buf, strlen(buf));
            headers_pointer+=strlen(buf);
        }
        
        Rio_readlineb(rp, buf, MAXLINE);
        // printf("%s", buf);
    }
    if(!is_host){
        strncpy(headers_pointer, host, strlen(host));
        headers_pointer+=strlen(host);
        strncpy(headers_pointer, hostname, strlen(hostname));
        headers_pointer+=strlen(hostname);
        strncpy(headers_pointer, "\r\n", 2);
        headers_pointer+=2;
    }
        if(!is_ua){
        strncpy(headers_pointer, user_agent, strlen(user_agent));
        headers_pointer+=strlen(user_agent);
        strncpy(headers_pointer, user_agent_hdr, strlen(user_agent_hdr));
        headers_pointer+=strlen(user_agent_hdr);
        strncpy(headers_pointer, "\r\n", 2);
        headers_pointer+=2;
    }
        if(!is_connection){
        strncpy(headers_pointer, connection, strlen(connection));
        headers_pointer+=strlen(connection);
        strncpy(headers_pointer, "close", 5);
        headers_pointer+=5;
        strncpy(headers_pointer, "\r\n", 2);
        headers_pointer+=2;
    }
            if(!is_proxy_connection){
        strncpy(headers_pointer, proxy_connection, strlen(proxy_connection));
        headers_pointer+=strlen(proxy_connection);
        strncpy(headers_pointer, "close", 5);
        headers_pointer+=5;
        strncpy(headers_pointer, "\r\n", 2);
        headers_pointer+=2;
    }
    strncpy(headers_pointer, "\r\n", 2);
    headers_pointer+=2;
    strncpy(dest, headers, strlen(headers));
    return;
}
/* $end read_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *hostname, char *port, char *path)
{
    *port = "80";
    char *http_pos = strstr(uri, "//");
    char *port_pos = strstr(http_pos + 2, ":");
    char *path_pos = strstr(http_pos + 2, "/");
    printf("%s", path_pos);
    char *end_pos = strstr(path_pos + 1, " ");
    printf("%s", end_pos);
    strncpy(hostname, http_pos + 2, (int)(strlen(http_pos + 2) - strlen(port_pos)));
    hostname[(int)(strlen(http_pos + 2) - strlen(port_pos))] = '\0';
    strncpy(path, path_pos, (int)(strlen(path_pos) - strlen(end_pos)));
    path[(int)(strlen(path_pos) - strlen(end_pos))] = '\0';
    strncpy(port, port_pos + 1, (int)(strlen(port_pos + 1) - strlen(path_pos)));
    port[(int)(strlen(port_pos + 1) - strlen(path_pos))] = '\0';
    return 1;
}
/* $end parse_uri */

/*
 * serve_static - copy a file back to the client 
 */
/* $begin serve_static */
void serve_static(int fd, char *filename, int filesize)
{
    int srcfd;
    char *srcp, filetype[MAXLINE], buf[MAXBUF];

    /* Send response headers to client */
    get_filetype(filename, filetype);    //line:netp:servestatic:getfiletype
    sprintf(buf, "HTTP/1.0 200 OK\r\n"); //line:netp:servestatic:beginserve
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n", filesize);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
    Rio_writen(fd, buf, strlen(buf)); //line:netp:servestatic:endserve

    /* Send response body to client */
    srcfd = Open(filename, O_RDONLY, 0);                        //line:netp:servestatic:open
    srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); //line:netp:servestatic:mmap
    Close(srcfd);                                               //line:netp:servestatic:close
    Rio_writen(fd, srcp, filesize);                             //line:netp:servestatic:write
    Munmap(srcp, filesize);                                     //line:netp:servestatic:munmap
}

/*
 * get_filetype - derive file type from file name
 */
void get_filetype(char *filename, char *filetype)
{
    if (strstr(filename, ".html"))
        strcpy(filetype, "text/html");
    else if (strstr(filename, ".gif"))
        strcpy(filetype, "image/gif");
    else if (strstr(filename, ".png"))
        strcpy(filetype, "image/png");
    else if (strstr(filename, ".jpg"))
        strcpy(filetype, "image/jpeg");
    else
        strcpy(filetype, "text/plain");
}
/* $end serve_static */

/*
 * serve_dynamic - run a CGI program on behalf of the client
 */
/* $begin serve_dynamic */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
    char buf[MAXLINE], *emptylist[] = {NULL};

    /* Return first part of HTTP response */
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Server: Tiny Web Server\r\n");
    Rio_writen(fd, buf, strlen(buf));

    if (Fork() == 0)
    { /* Child */ //line:netp:servedynamic:fork
        /* Real server would set all CGI vars here */
        setenv("QUERY_STRING", cgiargs, 1);                         //line:netp:servedynamic:setenv
        Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */    //line:netp:servedynamic:dup2
        Execve(filename, emptylist, environ); /* Run CGI program */ //line:netp:servedynamic:execve
    }
    Wait(NULL); /* Parent waits for and reaps child */ //line:netp:servedynamic:wait
}
/* $end serve_dynamic */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
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
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */
