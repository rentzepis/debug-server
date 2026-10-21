/**
 * proxy.c - main file
 *      can deal with HTTP/1.0 GET request
 *      dependency: csapp.h csapp.c
 *                  mylib.h mylib.c
 *                  cache.h cache.c
 */

#include <stdio.h>
#include "csapp.h"
#include "mylib.h"
#include "proxy.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr_new = "User-Agent: Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:64.0) Gecko/20100101 Firefox/64.0\r\n";

/* Global variables */
static Cache cache;

/* Functions */
void doit(int fd);
void read_requesthdrs(rio_t *rp, char* original_hdr);
void parse_uri(char* uri, char* hostname, char* port, char* filename);
void serve_static(int fd, char* filename, int filesize);
void serve_dynamic(int fd, char* filename, char* cgiargs);
void clienterror(int fd, char* cause, char* errnum,
                char* shortmsg, char* longmsg);
void make_proxy_request(char* request, char* request_line, 
                char* hostname, char* port);
void forward_request(int fd, char* request);
void forward_response(int serverfd, int clientfd, char* uri);
void* thread(void* vargp);

int main(int argc, char** argv)
{
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t client_len;
    pthread_t tid;
    struct sockaddr_storage client_addr;
    if(argc != 2){
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(1);
    }

    init_cache(&cache);
    sem_init(&wtmutex, 0, 1);
    sem_init(&rdmutex, 0, 1);
    Signal(SIGPIPE, SIG_IGN);   // install signal handler function

    listenfd = my_open_listenfd(argv[1]);
    dbg_printf("listenfd = %d ready\n", listenfd);
    while(1){
        client_len = sizeof(client_addr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA*)&client_addr, &client_len);
        if(!getnameinfo((SA*)&client_addr, client_len, hostname, MAXLINE, port,
            MAXLINE, 0)) //get hostname from client_addr and write to hostname
        {
            printf("Proxy accepted connection from (%s, %s)\n", 
                hostname, port);
            /* Split a thread to handle the connection independently */
            Pthread_create(&tid, NULL, thread, connfdp);
            // doit(*connfdp);
            // Close(*connfdp);
        }
    }
    dbg_printf("Proxy returns...\n");
    return 0;
}

/*
 * doit - handle one HTTP request/response transaction
 *      connect server and forward request to server;
 *      forward response to client
 */
void doit(int fd) 
{
    char buf[MAXLINE] = {0}, method[MAXLINE] = {0}, 
        uri[MAXLINE] = {0}, version[MAXLINE] = {0};
    char filename[MAXLINE] = {0}, hostname[MAXLINE] = {0}, port[MAXLINE] = {0};
    char original_hdr[MAXLINE]  = {0}, proxy_request[MAXLINE] = {0};
    cache_object* cache_obj;
    int proxyfd;
    rio_t rio;

    /* Read request line and headers */
    rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE))
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented",
                    "JOJO Proxy does not implement this method");
        return;
    }
    read_requesthdrs(&rio, original_hdr);
    /* Parse URI from GET request */
    parse_uri(uri, hostname, port, filename);

    /* Look for the file in cache before connect to server */
    /* Reader part */
    cache_obj = find_in_cache(uri, &cache);
    /* pass the content in the cache directly */
    if(cache_obj){
        forward_cache_content(fd, &cache, cache_obj);
    }

    /* Connect Web server and forward request if get a cache miss */
    else{   
        /* Writer part */
        /* Make proxy request */
        make_proxy_request(proxy_request, filename, hostname, port);
        dbg_printf("***Connecting server -- %s:%s***\n", hostname, port);
        if((proxyfd = my_open_clientfd(hostname, port)) < 0){
            dbg_printf("trying to connect to server...\nconnect failed\n");
            return;
        }
        dbg_printf("\nConnect successful! proxyfd = %d\n", proxyfd);
        /* Forward request from client to server */
        forward_request(proxyfd, proxy_request);
        /* Forward response from server to client */
        forward_response(proxyfd, fd, uri);
        /* Close proxyfd */
        Close(proxyfd);
    }
}

/*
 * parse_uri - parse URI into filename
 */
void parse_uri(char* uri, char* hostname, char* port, char* filename)
{
    dbg_printf("***in parse_uri***\n");

    char *host_begin, *port_begin, *host_end;
    /* locate hostname begin place */
    host_begin = strstr(uri, "://");    // uri in format "http://..."
    host_begin = (host_begin == NULL) ? uri : 
        host_begin + strlen("://"); 
    /* find the port */
    port_begin = index(host_begin, ':');
    /* locate hostname in the string */
    host_end = index(host_begin, '/');  // uri in format "http://.../"
    host_end = (host_end == NULL) ? uri + strlen(uri) : host_end;
    port_begin = (port_begin == NULL) ? host_end + 1 : 
        port_begin + 1;  // uri in format "http://...:.../"
    /* copy hostname */
    strncpy(hostname, host_begin, port_begin - host_begin - 1);//parse hostname
    dbg_printf("hostname: %s\n", hostname);
    /* copy port, default is 80 */
    if(host_end - port_begin > 0)
        strncpy(port, port_begin, host_end - port_begin);
    else
        strcpy(port, "80");
    dbg_printf("port: %s\n", port);
    /* copy filename, default is '/' */
    strcpy(filename, host_end);
    if(!strcmp(filename, "")){
        strcat(filename, "/");
    }
    dbg_printf("filename: %s\n", filename);
}

/*
 * clienterror - returns an error message to the client
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
    sprintf(body, "%s<hr><em>The JOJO Proxy</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

/*
 * read_requesthdrs - read HTTP request headers, and can just ignore them
 */
void read_requesthdrs(rio_t *rp, char* original_hdr) 
{
    dbg_printf("***in read_requesthdrs***\n");

    char buf[MAXLINE];

    memset(original_hdr, 0, MAXLINE);
    rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    if(FORWARD_HDR(buf)){
        strcat(original_hdr, buf);
    }
    while(strcmp(buf, "\r\n")) {
        rio_readlineb(rp, buf, MAXLINE);
        if(FORWARD_HDR(buf)){
            strcat(original_hdr, buf);
        }   
        printf("%s", buf);
    }
    return;
}

/**
 * make_proxy_request - form a HTTP request and HTTP header
 */
void make_proxy_request(char* request, char* request_line, 
    char* hostname, char* port)
{
    dbg_printf("***in make_proxy_request***\n");
    /* Initiate request */
    memset(request, 0, MAXLINE);
    /* Make HTTP request line */
    sprintf(request, "GET %s HTTP/1.0\r\n", request_line);
    /* Make HTTP request header */
    sprintf(request, "%sHost: %s:%s\r\n", request, hostname, port);
    sprintf(request, 
        "%s%sConnection: close\r\nProxy-Connection: close\r\n\r\n",
        request, user_agent_hdr_new);
    dbg_printf("%s", request);
}

/**
 * forward_request - forward request from local client
 *      but remember to change HTTP header
 */
void forward_request(int fd, char* request)
{
    dbg_printf("***in forward_request***\n");
    rio_writen(fd, request, strlen(request));
    dbg_printf("Request sended ok!\n");
}

/**
 * forward_response - read and forward response from server to local client
 *      need to cache
 */
void forward_response(int serverfd, int clientfd, char* uri)
{
    dbg_printf("***in forward_response***\n");
    rio_t rio;
    int filesize;
    char receive[MAXLINE] = {0}, buf[MAXLINE] = {0},
        header[MAXLINE] = {0}, *file;
    char *size_begin, *size_end;

    /* Get response from server if cache_obj is NULL */
    rio_readinitb(&rio, serverfd);

    rio_readlineb(&rio, receive, MAXLINE);
    while(strcmp(receive, "\r\n")){
        if((size_begin = strstr(receive, "Content-length: ")) ||
            (size_begin = strstr(receive, "Content-Length: "))){
            size_begin += strlen("Content-length: ");
            if((size_end = strstr(size_begin, "\r\n")) != NULL){
                strncpy(buf, size_begin, size_end - size_begin);
                dbg_printf("buf = %s\n", buf);
                filesize = atoi(buf);
                dbg_printf("filesize = %d\n", filesize);
            }
        }
        strcat(header, receive);
        rio_readlineb(&rio, receive, MAXLINE);
    }
    strcat(header, receive);
    dbg_printf("------header------\n%s", header);

    file = Malloc(filesize);
    rio_readnb(&rio, file, filesize);
    /* Send response to client */
    dbg_printf("Writing file to client...\n");
    rio_writen(clientfd, header, strlen(header));
    rio_writen(clientfd, file, filesize);
    dbg_printf("Writing finished.\n");
    /* Refreshing cache */
    if(filesize > 0){
        make_new_cache(&cache, file, uri, header, filesize);
        free(file);
    }
    return;
}

/**
 * thread - thread routine, run doit and Close
 *      solution: lock, WRITER FIRST. This is because total cache size
 *          if small, and a reader is very likely to be a writer later.
 *          The locked functions are find_in_cache(), insert_cache().
 *      Try READER FIRST now
 */
void* thread(void* vargp)
{
    int fd = *((int*)vargp);
    Pthread_detach(pthread_self());
    free(vargp);
    doit(fd);
    perror(strerror(errno));
    Close(fd);
    dbg_printf("closed successully.\n");
    return NULL;
}