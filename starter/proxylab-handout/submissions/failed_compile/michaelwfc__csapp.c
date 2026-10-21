#include <stdio.h>
// #include <socket.h>
#include <sys/socket.h>
#include <csapp.h>
#include <sbuf.h>
#include <signal.h>
#include <cache.h>

/* Misc constants */
#define MAXLINE 8192 /* Max text line length */
#define MAXBUF 8192  /* Max I/O buffer size */
#define LISTENQ 1024 /* Second argument to listen() */

#define NTHREADS 4
#define SBUFSIZE 16

sbuf_t sbuf; /* Shared buffer of connected descriptors */

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void handleRequest(int connfd);
void parse_request(int connfd, char *method, char *url, char *server_host, char *server_port, char *path, char *version,
                   char *client_user_agent, char *client_connection, char *client_headers);
void buildProxyRequest(char *request, char *method, char *server_host, char *server_port, char *path, char *version,
                       char *client_user_agent, char *client_connection, char *client_headers);
// int parse_url(char *url, char *filename, char *cgiargs);
void forwardRequest(int connfd, char *host, char *port, char *request, char *response, size_t *response_size);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

void *thread(void *vargp);

void sigpipe_handler(int sig)
{
    fprintf(stderr, "Caught SIGPIPE: lost connection\n");
}

/**
 * listing on port
 * when a connenction from client:
 * - parse the request
 * - check if the request is cached
 * - if not, send the request to the server and cache the response
 */
CacheList *cache;

int main(int argc, char **argv)
{
    printf("start proxy at port:%s\n", argv[1]);

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // initial the cache
    cache = cache_build();

    char *proxy_port = argv[1];

    int listenfd, connfd, i;
    char hostname[MAXLINE];
    char client_port[6]; // Enough to hold max 5 digits + null terminator
    pthread_t tid;

    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    // listen on the proxy port
    listenfd = Open_listenfd(proxy_port);

    // instial the sbuf
    sbuf_init(&sbuf, SBUFSIZE);
    // the main thread creates the set of worker threads
    for (i = 0; i < NTHREADS; i++) /* Create worker threads */
        Pthread_create(&tid, NULL, thread, NULL);

    signal(SIGPIPE, sigpipe_handler);

    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, client_port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, client_port);

        // inserting the resulting connected descriptors in sbuf.
        sbuf_insert(&sbuf, connfd); /* Insert connfd in buffer */

        // handleRequest(connfd);
        // close(connfd);
    }
    cache_free(cache);

    return 0;
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while (1)
    {
        // It waits until it is able to remove a connected descriptor from the buffer
        int connfd = sbuf_remove(&sbuf); /* Remove connfd from buffer */ // line:conc:pre:removeconnfd
        handleRequest(connfd);                                           /* Service client */
        Close(connfd);
    }
}

/**
 * handleRequest - handle a single HTTP request/response transaction
 * accept incoming connections,
 * read and parse requests,
 * forward requests to web servers,
 * read the servers’ responses,
 * forward those responses to the corresponding clients.
 */

void parse_url(char *url, char *host, char *port, char *path)
{
    char *host_start, *port_start, *path_start;

    // Skip the "http://" prefix
    if (strncmp(url, "http://", 7) == 0)
        host_start = (char *)url + 7;
    else
        host_start = (char *)url;

    // Find the beginning of path
    path_start = strchr(host_start, '/');
    if (path_start)
    {
        strcpy(path, path_start); // Copy path like /home.html
    }
    else
    {
        strcpy(path, "/"); // Default path
    }

    char host_port[MAXLINE];
    // Null-terminate the host:port part temporarily
    if (path_start)
    {
        size_t host_len = path_start - host_start;
        strncpy(host_port, host_start, host_len);
        host_port[host_len] = '\0';
    }
    else
    {
        strcpy(host_port, host_start);
    }
    // Split host and port
    port_start = strchr(host_port, ':');
    if (port_start)
    {
        *port_start = '\0'; // Split the string at ':'
        strcpy(host, host_port);
        strcpy(port, port_start + 1);
    }
    else
    {
        strcpy(host, host_port);
        strcpy(port, "80"); // Default HTTP port
    }
}

void parse_request(int connfd, char *method, char *url, char *server_host, char *server_port, char *path, char *version,
                   char *client_user_agent, char *client_connection, char *client_headers)
{
    // parse the request from client :
    // ex:  curl -v --proxy http://localhost:8000/ http://localhost:8001/home.html  (8000 is proxy port)
    // *   Trying 127.0.0.1:8000...
    // * TCP_NODELAY set
    // * Connected to localhost (127.0.0.1) port 8000 (#0)
    // > GET http://localhost:8001/home.html HTTP/1.1              //request line
    // > Host: localhost:8001                                      //request headers
    // > User-Agent: curl/7.68.0
    // > Accept: */*
    // > Proxy-Connection: Keep-Aliv
    // \r\n                                                        //Empty text line:
    printf("\nProxy: parsing request from client:\n");
    char buf[MAXLINE];
    rio_t rio;
    Rio_readinitb(&rio, connfd);

    // parse the request line of client request to get method, url, version
    if (Rio_readlineb(&rio, buf, MAXLINE) == 0)
    {
        return;
    }
    sscanf(buf, "%s %s %s", method, url, version);
    // According to the HTTP specification, proxies must receive the full URL (including scheme, host, and path) in the request line,
    // GET http://localhost:8001/home.html HTTP/1.1
    // so the proxy can parase and  contacts the real server, then they can forward it correctly.
    printf("%s %s %s\n", method, url, version);

    // parse the host ,port(optinal) and path from the full URL
    parse_url(url, server_host, server_port, path);

    // parse the request headers
    char host_in_header[MAXLINE];
    char proxy_connection[MAXLINE];
    while (Rio_readlineb(&rio, buf, MAXLINE) > 0)
    {
        printf("%s", buf);
        if (strcmp(buf, "\r\n") == 0)
        {
            // Empty text line: \r\n
            break;
        }
        else if (strncmp(buf, "Host: ", 6) == 0)
        {
            strncpy(host_in_header, buf + 6, strlen(buf) - 8); // Remove "Host: " and "\r\n"
            // host_in_header[strlen(buf) - 8] = '\0';
            // // Find the colon
            // char *colon = strchr(host_in_header, ':');
            // if (colon != NULL)
            // {
            //     *colon = '\0'; // Null-terminate the hostname
            //     strcpy(host, host_in_header);
            // }
            // else
            // {
            //     strcpy(host, host_in_header);
            // }
        }
        else if (strncmp(buf, "User-Agent: ", 12) == 0)
        {
            // get the client User-Agent header
            strncpy(client_user_agent, buf + 12, strlen(buf) - 14);
            client_user_agent[strlen(buf) - 14] = '\0';
        }
        else if (strncmp(buf, "Connection: ", 12) == 0)
        {
            // get the client Connection header
            strncpy(client_connection, buf + 12, strlen(buf) - 14);
            client_connection[strlen(buf) - 14] = '\0';
        }
        else if (strncmp(buf, "Proxy-Connection: ", 18) == 0)
        {
            // get the client Connection header
            strncpy(proxy_connection, buf + 18, strlen(buf) - 20);
            proxy_connection[strlen(buf) - 20] = '\0';
        }
        else
        {
            strcat(client_headers, buf);
        }
    }
}

void buildProxyRequest(char *request, char *method, char *server_host, char *server_port, char *path, char *version,
                       char *client_user_agent, char *client_connection, char *client_headers)
{
    // Construct the request from proxy then send to the server
    // Request line: <method> <url> <version>
    // Request headers:
    // Empty text line: \r\n
    char host_port[MAXLINE];                                                   // Make sure it's large enough to hold both strings and colon
    snprintf(host_port, sizeof(host_port), "%s:%s", server_host, server_port); // Use snprintf() instead of sprintf() to avoid buffer overflows.

    char proxy_verion[MAXLINE] = "HTTP/1.0";
    sprintf(request, "%s %s %s\r\n", method, path, proxy_verion);  // relative url just as directortly request
    sprintf(request + strlen(request), "Host: %s\r\n", host_port); // put server_host_port in Host header
    sprintf(request + strlen(request), "User-Agent: %s", user_agent_hdr);
    sprintf(request + strlen(request), "%s", "Connection: close\r\n");
    sprintf(request + strlen(request), "%s", "Proxy-Connection: close\r\n");
    sprintf(request + strlen(request), "%s", client_headers);
    sprintf(request + strlen(request), "\r\n");
}
void handleRequest(int connfd)
{
    char method[MAXLINE], url[MAXLINE], version[MAXLINE], server_host[MAXLINE], server_port[6], path[MAXLINE],
        client_user_agent[MAXLINE], client_connection[MAXLINE], client_headers[MAXLINE];

    parse_request(connfd, method, url, server_host, server_port, path,
                  version, client_user_agent, client_connection, client_headers);

    char *cached_obj;
    int obj_size;
    
    // Allocate buffer
    char response[MAX_OBJECT_SIZE];
    size_t response_len = 0;

    if ((cached_obj = cache_get(cache, url, &obj_size)) != NULL)
    {
        rio_writen(connfd, cached_obj, obj_size);
        free(cached_obj);
        return;
    }
    else
    {
        // else, connect to server, stream response into buf

        if (strcasecmp(method, "POST") == 1)
        {
            // fprintf(stderr, "Not implemented\n");
            clienterror(connfd, url, "501", "Not Implemented", "Proxy does not implement this method");
            return;
        }
        char request[MAXLINE];
        buildProxyRequest(request, method, server_host, server_port, path, version, client_user_agent, client_connection, client_headers);

        // forward the request to the server
        printf("Proxy: Forwarding to %s:%s\n%s\n\n", server_host, server_port, request);
        /**
        GET /home.html HTTP/1.0
        Host: localhost:8001
        User-Agent: User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3
        Connection: close
        Proxy-Connection: close
        Accept: */
        /*
         */
        forwardRequest(connfd, server_host, server_port, request, response, &response_len);

        // while also writing to cache buffer
        Cache_put(cache, url, response, response_len);
    }
}

/**
 * binary data: Can contain null bytes (\0), which means you cannot treat it like a normal string.
 * Needs to be handled with byte counts, not string functions like strlen.
 * If you’re forwarding HTTP responses with binary content, parse the HTTP headers to get:  Content-Length: 84923
 *
 * Bonus: Chunked Transfer Encoding?
If the server uses chunked transfer encoding (in HTTP/1.1), you’ll need to:
- Parse chunk sizes
- Read chunks accordingly
- Handle \r\n correctly
 */
void forwardRequest(int connfd, char *host, char *port, char *request, char *response, size_t *response_len)
{
    int n;
    size_t totol_size=0;
    char buf[MAXLINE];

    // the proxy clientfd
    int clientfd;
    // open the connection from proxy to the server: proxy -> server
    rio_t rio;

    clientfd = Open_clientfd(host, port);
    // initialize the rio for clienfd
    Rio_readinitb(&rio, clientfd);

    // Send the request to the server
    Rio_writen(clientfd, request, strlen(request));

    // block and wait the response from the server
    // read the response from the server until the end of the response
    printf("Proxy Receiving  response from %s:%s\n", host, port);

    while ((n = Rio_readnb(&rio, buf, MAXBUF)) > 0) // Read from server and write to client until EOF or error
    {
        // printf("%s", buf); // might stop early on binary data  include null bytes (\0)
        /* This is streaming, not buffering the full data. It works well even for large files.
         the client will start receiving data as each write() call is made — not only after the loop finishes.
        The key here is that write(client_fd, buf, n) sends data over a TCP socket. TCP is a streaming protocol, so:
            1. Each write() pushes bytes into the TCP send buffer.
            2. The kernel starts transmitting the data immediately (subject to buffering and flow control).
            3. The client can start receiving the data incrementally, often even before your while loop finishes.
         */
        Rio_writen(connfd, buf, n);
        totol_size += n;
        if(totol_size < MAX_OBJECT_SIZE+1)
            memcpy(response + *response_len, buf, n);
        // keep recording the response_len
        *response_len += n;
    }

    Close(clientfd);
}
/* $end echoclientmain */

/**
 * HTTP request parser
When an end user enters a URL such as http://www.cmu.edu/hub/index.html into the address bar of a web browser, the browser will send an HTTP request to the proxy that begins with a line that might resemble the following:
`GET http://www.cmu.edu/hub/index.html HTTP/1.1`
In that case, the proxy should parse the request into at least the following fields:
- the hostname, www.cmu.edu;
- the path or query and everything following it, /hub/index.html.
*/

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
    sprintf(buf, "<html><title>Proxy Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Proxy</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
/* $end clienterror */