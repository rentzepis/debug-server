/* 
 * proxy.c
 *
 * Author: Kais Kudrolli
 * Andrew ID: kkudroll
 *
 * File description: this file contains the C code for a basic web proxy
 * that can handle HTTP GET requests. The proxy acts as a server and listens
 * for client connections. When a client (web browser) connects, it sends an
 * HTTP request. If this is a GET request, the proxy acts as a client and 
 * connects to the appropriate web server based on the URI in the request.
 * It forwards the request to the server and forwards the subsequent response
 * to the browser. This proxy uses a concurrency model based on threads: it
 * creates a new thread for each new request that is received from a client.
 * The proxy also utilizes a web cache to speed up web object access. It 
 * caches web objects within a certain size limit as it forwards the server
 * response to the client, and it does a cache lookup as soon as is gets the
 * server URI from the GET request. If the web object is cached, the proxy 
 * simply forwards the object to the client and does not need to make a 
 * connection to a web server.
 *
 */

#include <stdio.h>
#include <assert.h>

#include "csapp.h"
#include "cache.h"

/* Macros */
#define DEFAULT_CLIENT_PORT 80  /* If a port is not specified in
                                   the URI, this is the default port
                                   the proxy tries to connect to on
                                   the web server. */

/* Global Variables */
/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
pthread_rwlock_t cache_lock; /* Read/Write lock for web cache */
Cache *cache;                /* Cache for web objects */

/* 
 * Function Prototypes 
 */
/* Main proxy functions */
void *thread(void *connfdp);
void doit(int connfd);
int handle_request(int fd, char *host, char *uri, int *client_port, 
        int *clientfd); 
void get_response(int clientfd, int connfd, char *uri);
void read_requesthdrs(rio_t *rp, char *host_hdr, int clientfd); 
void parse_uri(char *uri, char *hostname, char *path, int *client_port); 
/* Warning wrapper functions */
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);
int Open_clientfd_w(char *hostname, int port); 



/*
 * Main proxy functions
 * --------------------
 */

/*
 * main - The proxy's main routine. It performs all the necessary 
 * initializations and then enter an infinite server loop.
 *
 * Parameters:
 *  - argc: the number of commandline arguments given to main
 *  - argv: the array of commandline arguments
 * Return value:
 *  - 0: on succesful exit of the program
 *  - 1: exit on error
 */
int main(int argc, char **argv) 
{
    int listenfd;
    int *connfdp;
    int listen_port;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;

    /* Handle SIGPIPE */
    Signal(SIGPIPE, SIG_IGN);

    /* Initialize web cache */
    cache = cache_init();

    /* Initialize cache read/write lock */
    Pthread_rwlock_init(&cache_lock, NULL);

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    /* Open a port and listen for client connections */
    listen_port = atoi(argv[1]);
    listenfd = Open_listenfd(listen_port);

    /* Infinite server loop */
    while (1) {
        /* Accept a connection and create a new thread for the request */
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}

/* 
 * thread - This function is executed whenever a new thread is created.
 * The thread detaches itself so that it does not need to be reaped by 
 * another thread, and the workhorse function, doit(), of the proxy is
 * called. Then the conenction is closed.
 *
 * Parameter:
 *  - connfdp: pointer to the connection file descriptor
 * Return value:
 *  - NULL
 */
void *thread(void *connfdp)
{
    int connfd = *((int *)connfdp);
    Pthread_detach(pthread_self());
    Free(connfdp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

/*
 * doit - This is the workhorse function of the proxy. It starts the
 * request-handling of the HTTP request and forwards the server 
 * response to the web browser.
 *
 * Parameter:
 *  - connfd: connection file descriptot
 */
void doit(int connfd)
{
    char host[MAXLINE];
    char uri[MAXLINE];
    int client_port;
    int clientfd;
    int request_ok;

    /* Handle the request sent by the browser */
    request_ok = handle_request(connfd, host, uri, &client_port, &clientfd);

    /* Forward the server response */
    if (request_ok) {
        get_response(clientfd, connfd, uri);
        Close(clientfd);
    }

    /* Zero out the host buffer */
    memset(host, 0, strlen(host));
    return;
}

/* 
 * get_response - This function reads the server response and forwards it
 * to the client. It also determines whether to cache the web object and does
 * so.
 *
 * Parameters:
 *  - clientfd: file descriptor of socket on the web server to which the
 *              proxy connects
 *  - connfd: file descriptor on which the client has connected to the
 *            proxy
 *  - uri: the URI of the web server
 */
void get_response(int clientfd, int connfd, char *uri) 
{
    rio_t rio;
    char buf[MAXBUF];
    char object_buf[MAX_OBJECT_SIZE];
    int obj_size = 0;
    int need_to_cache = 1;
    size_t read_count;

    strcpy(object_buf, "");

    Rio_readinitb(&rio, clientfd);
    
    /* Read and write the server response */
    while ((read_count = Rio_readnb_w(&rio, buf, MAXBUF)) > 0) {
        Rio_writen_w(connfd, buf, read_count);
        obj_size += read_count;

        /* Determine whether or not to cache the web object */
        if (obj_size <= MAX_OBJECT_SIZE) {
            strncat(object_buf, buf, read_count);
        } else {
            need_to_cache = 0;
        }
    }

    if (need_to_cache) {
        /* Cache the web object */
        cache_add(cache, uri, object_buf);
    }

    return;
}

/*
 * handle_request - This function handles all HTTP requests sent by
 * the client. If it is a get request, it forwards the request to the
 * server. Otherwise, it ignores the request.
 *
 * Parameters:
 *  - fd: the file descriptor of the client connection socket
 *  - host: the web server's host name
 *  - uri: URI from the request
 *  - client_port: pointer to the port number on the server to which
 *                 the proxy must connect
 *  - client_fd: pointer to the proxy's client file descriptor
 * Return value:
 *  - 0: the request was ignored or handled already (retrieved from the cache)
 *  - 1: the request warrants a response from the server
 */
int handle_request(int fd, char *host, char *uri, int *client_port, 
        int *clientfd) 
{
    char method[MAXLINE], version[MAXLINE];
    char request_line[MAXLINE];
    char temp[MAXLINE];
    char path[MAXLINE];
    char content[MAX_OBJECT_SIZE];
    rio_t rio; 
    int hit = 0;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb_w(&rio, temp, MAXLINE);
    sscanf(temp, "%s %s %s", method, uri, version);

    /* Determine if the request is a GET request */
    if (strcasecmp(method, "GET")) { 
        fprintf(stderr, "%s method is not implemented\n", method);
        return 0;
    }

    /* If the content at the URI is cached, just write the content */
    hit = cache_lookup(cache, uri, content);
    if (hit) {
        Rio_writen_w(fd, content, strlen(content));
        return 0;
    }

    /* Parse URI from GET request */
    parse_uri(uri, host, path, client_port); 

    /* Open connection to web server */
    *clientfd = Open_clientfd_w(host, *client_port);
    if (*clientfd < 0) {
        return 0;
    }

    /* Write request line of response */
    sprintf(request_line, "GET %s HTTP/1.0\n", path);
    Rio_writen_w(*clientfd, request_line, strlen(request_line));

    /* Read the request headers */
    read_requesthdrs(&rio, host, *clientfd);
    
    return 1;
}

/*
 * read_requesthdrs - This function reads and parses HTTP request headers.
 * It replaces select headers with predefined ones in order to coax
 * sensible responses from some web servers. It forwards the rest 
 * unaltered.
 *
 * Parameters:
 *  - rp: pointer to persistent rio package state
 *  - host_hdr: header that contains the host
 *  - clientfd: file descriptor for proxy's client socket, to which it
 *              connects on the web server
 */
void read_requesthdrs(rio_t *rp, char *host_hdr, int clientfd) 
{
    char buf[MAXLINE];
    char conn[MAXLINE];
    char proxy_conn[MAXLINE];
    char host[MAXLINE];
    int host_seen = 0;
    ssize_t n;

    /* Loop over all the request headers */
    while ((n = Rio_readlineb_w(rp, buf, MAXLINE)) != 0) {
        if (!strncmp(buf, "\r\n", strlen(buf))) {
            /* At end of the request headers */

            /* 
             * If there is a host header write it to the server.
             * Otherwise, create a host header and write that.
             */
            if (!host_seen) {
                sprintf(host, "Host: %s\r\n", host_hdr);
                Rio_writen_w(clientfd, host, strlen(host));
            }
            
            /* Write the predefined headers that must always be the same */
            Rio_writen_w(clientfd, (void *)user_agent_hdr, 
                    strlen(user_agent_hdr));
            Rio_writen_w(clientfd, (void *)accept_hdr, 
                    strlen(accept_hdr));
            Rio_writen_w(clientfd, (void *)accept_encoding_hdr, 
                    strlen(accept_encoding_hdr));
            sprintf(conn, "Connection: close\r\n");
            sprintf(proxy_conn, "Proxy-Connection: close\r\n");
            Rio_writen_w(clientfd, conn, strlen(conn));
            Rio_writen_w(clientfd, proxy_conn, strlen(proxy_conn));
            Rio_writen_w(clientfd, buf, strlen(buf));
            break;
        } else {
            /* Determine whether a request already has a host header */
            if (!strncmp(buf, "Host", strlen("Host"))) {
                host_seen = 1;
                Rio_writen_w(clientfd, buf, strlen(buf));
            } else if (!strncmp(buf, "User-Agent", strlen("User-Agent"))
                        || !strncmp(buf, "Accept", strlen("Accept"))
                        || !strncmp(buf, "Accept-Encoding", 
                            strlen("Accept-Encoding"))
                        || !strncmp(buf, "Connection", strlen("Connection"))
                        || !strncmp(buf, "Proxy-Connection", 
                            strlen("Proxy-Connection"))) {
                /* 
                 * If there is a header for any of the predefined ones,
                 * ignore it, and zero out the buffer.
                 */
                memset(buf, 0, strlen(buf));
                continue;
            } else {
                /* Simply forward all other headers */
                Rio_writen_w(clientfd, buf, strlen(buf));
            }
        }
        memset(buf, 0, strlen(buf));
    }

    return;
}

/*
 * parse_uri - This functions parses the URI to determine the hostname,
 * path of the web object, and the client port to which the proxy must 
 * connect.
 *
 * Parameters:
 *  - uri: the URI of the content on the web server
 *  - hostname: the server's host name
 *  - path: the path of the web oject on the server
 *  - client_port: port on web server to which the proxy must connect
 */
void parse_uri(char *uri, char *hostname, char *path, int *client_port) 
{
    char ptr[MAXLINE];
    char port_string[MAXLINE];
    char port[MAXLINE];
    size_t host_count = 0;
    size_t port_count = 0;
    size_t i;
    
    strcpy(ptr, uri);
    
    /* Get rid of "http://" if its there */
    if (strstr(ptr, "://")) {
        strcpy(ptr, index(ptr, ':'));
        strcpy(ptr, &ptr[3]);
    }

    /* Copy up until the first '/' to get the host name */
    assert(ptr != NULL);
    for (i = 0; (ptr[i] != '/') && (ptr[i] != '\0') && (ptr[i] != ':'); i++) {
        host_count++;
    }
    strncpy(hostname, ptr, host_count);

    /* Determine what the client port is */
    if (ptr[i] == ':') {
        /* A client port is specified in the URI */
        i += 1;
        strcpy(port_string, &ptr[i]);
        for ( ; isdigit(ptr[i]); i++) {
            port_count++;
        }
        strncpy(port, port_string, port_count);
        *client_port = atoi(port);
    } else {
        /* Use the default client port 80 */
        *client_port = DEFAULT_CLIENT_PORT;
    }

    /* The rest is the path of the web object */
    strcpy(path, &ptr[i]);

    memset(ptr, 0, strlen(ptr));
    return;
}

/*
 * End main proxy functions
 * ------------------------
 */


/* 
 * Warning wrapper functions 
 * -------------------------
 * These funtions wrap the rio functions so that they do not
 * exit the proxy if there is an error.
 */

ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n)
{
    ssize_t rtn;
    if ((rtn = rio_writen(fd, usrbuf, n)) < 0) {
        fprintf(stderr, "Error during write\n");
    }
    return rtn;
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen)
{
    ssize_t rtn;
    if ((rtn = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        fprintf(stderr, "Error in readline\n");
    }
    return rtn;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n)
{
    ssize_t rtn;
    if ((rtn = rio_readnb(rp, usrbuf, n)) < 0) {
        fprintf(stderr, "Error in readnb\n");
    }
    return rtn;
}

int Open_clientfd_w(char *hostname, int port) 
{
    int rtn;
    if ((rtn = open_clientfd_r(hostname, port)) < 0) {
        fprintf(stderr, "Error in open_clientfd\n");
    }
    return rtn;
}

/*
 * End warning wrapper functions
 * -----------------------------
 */
