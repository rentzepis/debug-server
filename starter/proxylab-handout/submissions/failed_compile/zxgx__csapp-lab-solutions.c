#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define THREAD_POOL_SIZE 8

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
sbuf_t sbuf;

void client_error(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
    char buf[MAXLINE], body[MAXBUF];

    /* Format the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The proxy lab</em>\r\n", body);

    /* Print the HTTP response headers & body */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    Rio_writen(fd, body, strlen(body));
}

int parse_url(char *url, char *hostname, char *port, char *suffix){
    char buf[MAXLINE], *ptr, *port_ptr;

    sscanf(url, "%*[^:]://%s", buf); // strip protocol like http, https etc.

    ptr = buf;
    while(*ptr && *ptr!='/' && *ptr!=':') ptr++;

    if(*ptr == ':'){
        *ptr = '\0';
        strcpy(hostname, buf);
        
        ptr++;
        port_ptr = ptr;
        while(*ptr!='/') ptr++;
        *ptr = '\0';
        strcpy(port, port_ptr);
    } else if(*ptr == '/'){
        *ptr = '\0';
        strcpy(hostname, buf);
        port[0] = '\0';
    } else
        return -1;
    *ptr = '/';
    strcpy(suffix, ptr);
    return 0;
}

void iterative_run(int fd){
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[8], suffix[MAXLINE], *host_header;
    char request[MAX_OBJECT_SIZE], *request_ptr = request;
    rio_t client_rio, server_rio;
    int serverfd;
    ssize_t request_cnt;

    // read request line
    Rio_readinitb(&client_rio, fd);
    request_cnt = Rio_readlineb(&client_rio, request_ptr, MAXLINE);
    printf("Request line from client:\n%s", request_ptr);

    // parse request line and target url
    if(sscanf(request_ptr, "%s %s %s", method, url, version)!=3 || parse_url(url, hostname, port, suffix) == -1){
        client_error(fd, method, "400", "Bad request", "Proxy received bad request from client");
        return;
    }
    if(strcasecmp(method, "GET")){
        client_error(fd, method, "501", "Not implemented", "Proxy does not implement this method");
        return;
    }
    
    // reformat requested file
    sprintf(request, "GET %s HTTP/1.0\r\n", suffix);
    request_cnt = strlen(request);

    // copy all the request headers
    while(strcmp(request_ptr, "\r\n")){
        request_ptr += request_cnt;
        request_cnt = Rio_readlineb(&client_rio, request_ptr, MAXLINE);
    }

    // append Host header if missing
    if((host_header= strstr(request, "Host: ")) == NULL){
        sprintf(request_ptr, "Host: %s\r\n", hostname);
        request_ptr += strlen(request_ptr);
    }

    if((host_header=strstr(request, "Proxy-Connection: "))==NULL){
        sprintf(request_ptr, "Proxy-Connection: close\r\n");
        request_ptr += strlen(request_ptr);
    }

    if((host_header=strstr(request, "\nConnection: "))==NULL){
        sprintf(request_ptr, "Connection: close\r\n");
        request_ptr+= strlen(request_ptr);
    }

    if((host_header= strstr(request, "User-Agent: ")) == NULL){
        sprintf(request_ptr, "%s", user_agent_hdr);
        request_ptr += strlen(request_ptr);
    }
    sprintf(request_ptr, "\r\n");
    
    printf("Target host: %s:%s, filename: %s\n\nRequest:\n%s", hostname, port, suffix, request);
    
    /**********************************************/

    // init connection to server
    serverfd = Open_clientfd(hostname, strlen(port)>0?port:NULL);
    Rio_readinitb(&server_rio, serverfd);

    // send request to server
    Rio_writen(serverfd, request, strlen(request));

    /**********************************************/
    
    char content_type[MAXLINE];
    unsigned long content_len=0;
    strcpy(content_type, "text/html");

    // parse response line & headers from server
    request_ptr = request;
    request_cnt = Rio_readlineb(&server_rio, request_ptr, MAXLINE);
     (fd, request_ptr, request_cnt);
    while(strcmp(request_ptr, "\r\n")){
        request_ptr += request_cnt;
        request_cnt = Rio_readlineb(&server_rio, request_ptr, MAXLINE);

        /* It seems that the proxy should not forward response line & headers to client*/
        // Rio_writen(fd, request_ptr, request_cnt);
        
        if((host_header = strstr(request_ptr, "Content-type: "))!=NULL){
            sscanf(request_ptr, "Content-type: %[^\r]", content_type);
        } else if((host_header = strstr(request_ptr, "Content-length: "))){
            sscanf(request_ptr, "Content-length: %ld", &content_len);
        }
    }
    printf("Proxy recieved response:\n%s", request);
    printf("Proxy recieved response body of len %ld:\n", content_len);

    // forward response body
    if(content_len > 0){
        if(strstr(content_type, "text")) { // text
            while((request_cnt = Rio_readlineb(&server_rio, request, MAXLINE))>0){
                Rio_writen(fd, request, request_cnt);
                printf("%s", request);
            }
        } else {  // binary
            printf("omit binary...\n");
            while((request_cnt = Rio_readnb(&server_rio, request, content_len))>0){
                Rio_writen(fd, request, request_cnt);
            }
        }
    }

    Close(serverfd);
    printf("Server connection is closed\n");
}

void *thread(void *vargp){
    Pthread_detach(pthread_self());
    int connfd;
    while(1){
        connfd = sbuf_remove(&sbuf);
        iterative_run(connfd);
        Close(connfd);
    }
}

int main(int argc, char *argv[])
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage client_addr;
    pthread_t tid;

    if (argc!=2){
        sprintf(hostname, "usage: %s <port>", argv[0]);
        app_error(hostname);
    }

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, THREAD_POOL_SIZE);
    for(int i=0; i<THREAD_POOL_SIZE; i++)
        Pthread_create(&tid, NULL, thread, NULL);

    clientlen = sizeof(client_addr);
    while(1){
        connfd = Accept(listenfd, &client_addr, &clientlen);

        Getnameinfo(&client_addr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Proxy accepts connection from client (%s : %s)\n", hostname, port);

        sbuf_insert(&sbuf, connfd);
    }

    sbuf_deinit(&sbuf);
    return 0;
}
