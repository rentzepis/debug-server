/*
 proxy.c for proxy lab
 ----------------------
 Designer: Guanting Liu
 Andrew ID: guantinl
 Date: April 28, 2014
 ----------------------

    Recommended websites for testing Proxy:
    ---------------------------------------
    www.wikipedia.org
    {tiny server}
    {tiny server}/cgi-bin/adder?123&456


  About proxy design
 ---------------------
    This is a simple, iterative HTTP/1.0 Web proxy that can handle GET 
 method to serve static and dynamic content. 

    This proxy supports caching (See "cache.c" and "cache.h" for detail). 
 It can cache the actual individual server response content smaller than 
 MAX_OBJECT_SIZE, into its sized cache of up to MAX_CACHE_SIZE. WHenever 
 a request is found to have a corresponding cached response content in 
 this proxy, in order to improve the browsing speed, the proxy would 
 directly used the cached content to reply the requesting client, without 
 forwarding the request to the original server and get the content again.

    This proxy also supports multithreading. It creates a separate thread 
 to serve each request from the same or different client(s). By serving 
 these requests concurrenly, it improves the browsing speed. 
    
    Note: some macro constants or global variables are defined or declared 
in cache.h

---------------------------------------------------------------------------
       Sync functions reference:
                                           
       pthread_mutex_t mutex;
       pthread_mutex_init(&mutex, 0);
       pthread_mutex_lock(&mutex);
       pthread_mutex_unlock(&mutex);

       pthread_rwlock_t lock;
       pthread_rwlock_init(&lock, NULL);
       pthread_rwlock_rdlock(&lock);
       pthread_rwlock_wrlock(&lock);
       pthread_rwlock_unlock(&lock);
    
    Pthread_rwlock is a handy reliable reader-writer lock available in the 
 pthread.h library. It gives writers priority over readers but effectively 
 prevents starvation. This proxy uses pthread_rwlock to synchronize cache 
 accesses.

---------------------------------------------------------------------------
    There were several small modifications to csapp.c and csapp.h:

    1. The Rio_writen() was modified to have a ssize_t return type for the 
 convenience of error checking.

    2. unix_error_nexit() was added to print unix-style error witou exiting.
  It prevents program termination caused by subtle communication errors such 
  as a broken socket.

    3. Function wrappers Rio_readn(), Rio_writen(), Rio_readnb(), 
  Rio_readlineb(), Open_clientfd_r(), Malloc() were modified to use 
  unix_error_nexit();
 */



#define _GNU_SOURCE     /* For using non-standard function strcasestr() */
#include "csapp.h"
#include "cache.h"

/* Macro constants */
#define DEFAULT_PORT 80;    /* Defualt port number for forwading request */
#define MAX_THREAD_ID 100   /* Maximum ID of background threads */
    /* Note: 
     *
     *    This parameter DOES NOT limit the allowable number of threads that
     * can run in the back ground concurrently. Thread ID is just a man-made 
     * nominal number for conveniently distinguishing threads when there are 
     * only a few of them.
     *
     *    The number of running background threads can exceed this number.
     * In this case, some threads will share a same thread ID, but they are 
     * still different threads.
     * 
     *    This program DOES NOT pose any restriction on the number of 
     * concurrent threads.
     */      


/*
 *  Constants
 */
/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux \
x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,\
application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";



/*
 *  Global/shared variables
 */
Cache_List cache_list;
unsigned int thread_count = 0;      /* Number of running background threads */
pthread_mutex_t thread_count_mutex;

/*
 *  Function Prototypes
 */
char *strcasestr(const char *haystack, const char *needle); /* GNU function */

void *proxy_thread(void *args);

int read_and_parse_request(rio_t *rio_client, int *clientfd, int *serverfd, 
        char *buf, char *uir, char *host, char* new_request_buf);


int forward_request_to_server(rio_t *rio_server, int *serverfd, 
        int clientfd, char *host, char *hostname, int port, 
        char *new_request_buf);
int cache_and_forward_response(rio_t *rio_server, int clientfd, 
        char *usrbuf, char *uri, unsigned int *bytecount);
int separate_host_port(char *host, char *hostname, int *hostport);
void clienterror(int fd, char *cause, char *errnum, 
        char *shortmsg, char *longmsg);
void close_fd(int *serverfd, int *clientfd, int thread_id);
/* Wrappers for the pthread_mutex_* functions */
int Pthread_mutex_init(pthread_mutex_t *mutex,
        const pthread_mutexattr_t *attr);
int Pthread_mutex_lock(pthread_mutex_t *mutex);
int Pthread_mutex_unlock(pthread_mutex_t *mutex);




/* 
 * Main routine of the proxy
 */
int main(int argc, char **argv)
{
    int listenfd, *connfd, port, clientlen, thread_id;
    struct sockaddr_in clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);
    if (port <0 || port > 65535) {
        fprintf(stderr, "legal port number : 0 to 65535\n");
        exit(1);
    }

    /* Ignore SIGPIPE signals */
    Signal(SIGPIPE, SIG_IGN);
    init_cache_list(&cache_list);   /* safe to call */

    Pthread_mutex_init(&thread_count_mutex, 0);   
    Pthread_rwlock_init(&cache_rwlock, NULL);

    listenfd = Open_listenfd(port);

    printf("\n=================================================\n");
    printf("Welcome using this proxy! Listening on port %d\n", port);
    printf("-------------------------------------------------\n\n");

    thread_id = 0;
    while (1) {
        /* Asigned a nominal thread ID for the next thread */
        if (++thread_id > MAX_THREAD_ID)
            thread_id = 1;

        clientlen = sizeof(clientaddr);

        while ((connfd = (int *) Malloc(2 * sizeof(int))) == NULL) {
            sleep(1);   /* If run out of memory, wait for threads to free */
        }
        while ((*connfd = accept(listenfd, (SA *)&clientaddr, 
                (socklen_t *)&clientlen)) < 0)
        {
            /* Print the error and keep trying */
            unix_error("Accept error");
        }
        *(connfd + 1) = thread_id;

        /* Sequential proxy does not create any thread */
        // proxy_thread(connfd);

        /* Concurrent proxy creates a thread to serve each client request */
        pthread_t tid;
        while (pthread_create(&tid, NULL, proxy_thread, (void *)connfd) != 0) {
            /* Keep trying until successfully creating a thread */
            printf("pthread_create failed, will try again.\n");
        }
    }
    return 0;
}

/*
 *  Proxy thread routine created to serve an individual client request
 */
/*  Program should not exit from the thread except for critical error cases */
void *proxy_thread(void *args) {
    /* Program will eixt if pthread_detach() does not work */
    Pthread_detach(pthread_self());
    int clientfd = *(int *)args;
    int thread_id = *((int *)args + 1);
    Free(args);

    /* Thread Body */
    rio_t rio_client;
    rio_t rio_server;
    int serverfd = -1, port;
    char buf[MAXLINE], uri[MAXLINE];
    char host[MAXLINE], hostname[MAXLINE], uri_suffix[MAXLINE];
    char new_request_buf[MAXLINE], usrbuf[MAX_OBJECT_SIZE];
    unsigned int byte_count = 0;

    Pthread_mutex_lock(&thread_count_mutex);
    thread_count++;
    Pthread_mutex_unlock(&thread_count_mutex);
    printf("{ [%d] Client connected. \tCurrent Background threads: %d }\n\n", 
            thread_id, thread_count);



    Rio_readinitb(&rio_client, clientfd);   /* Safe to call */

    read_and_parse_request(&rio_client, &clientfd, &serverfd, buf, uir, host, 
            new_request_buf);

    /* URI string with port number as the cache sitem ID */
    sprintf(uri, "%s:%d%s", hostname, port, uri_suffix);

    /* Search uri in cache */
    /* If cache hit */
    if (search_and_get(&cache_list, uri, usrbuf, &byte_count) != -1) {
        printf("URI: %s\nCache Hit!\n\n", uri);

        /* Send response from cache */
        if (Rio_writen(clientfd, usrbuf, byte_count) == -1) {
            close_fd(&serverfd, &clientfd, thread_id);
            return NULL;
        }
    }
    /* If cache miss */
    else {
        printf("URI: %s\nCache Miss.\n\n", uri);

        if (forward_request_to_server(&rio_server, &serverfd, clientfd, host, 
                hostname, port, new_request_buf) == -1) 
        {
            close_fd(&serverfd, &clientfd, thread_id);
            return NULL;
        }

        cache_and_forward_response(&rio_server, clientfd, usrbuf, uri,
                &byte_count);
    }

    printf("\n(%d bytes have been transmited as response.)\n", byte_count);
    close_fd(&serverfd, &clientfd, thread_id);
    return NULL;
}

int read_and_parse_request(rio_t *rio_client, int *clientfd, int *serverfd, 
        char *buf, char *uir, char *host, char* new_request_buf)
{
    char *ptr;
    char version[MAXLINE], method[MAXLINE];
    int has_host_hdr = 0;

    /* Read the HTTP request line (the first line) */
    if (Rio_readlineb(rio_client, buf, MAXLINE) == -1) {
        close_fd(&serverfd, clientfd, thread_id);
        return NULL;
    }
    printf("Orignal request:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    /* Ignore non-GET methods */
    if (strcmp(method, "GET")) {
        printf("Unable to handle method type: %s\n\n", method);
        clienterror(*clientfd, host, "501", "Not Implemented",
                "Proxy does not implement this method");
        close_fd(&serverfd, clientfd, thread_id);
        return NULL;
    }

    /* Extract host from URI */
    if ((ptr = strstr(uri, "://"))) {
        strcpy(host, ptr + 3);
    } else {
        strcpy(host, uri);
    }
    if ((ptr = strstr(host, "/"))) {
        strcpy(uri_suffix, ptr);    /* Extract the uri suffix */
        *ptr = '\0';            /* cut suffix of URL to get host string */
    }
    printf("\t(Host extracted: %s)\n", 
            (strcmp(host, "\0")) ? host : "[Null]");
    
    /* Start reasembling the HTTP request */

    /* Reasemble HTTP/1.0 GET request line */
    strcpy(new_request_buf, "GET ");
    strcat(new_request_buf, uri_suffix);
    strcat(new_request_buf, " HTTP/1.0\r\n");

    /* Separate the hostname and hostport */
    if (separate_host_port(host, hostname, &port) == -1) {
        close_fd(&serverfd, clientfd, thread_id);
        return NULL;
    }

    /* Read other request header lines */
    while ((k = Rio_readlineb(rio_client, buf, MAXLINE)) != 0) {
        if (k == -1) {
            close_fd(&serverfd, clientfd, thread_id);
            return NULL;
        }

        if (!strcmp(buf, "\r\n")) {
            printf("%s", buf);
            break;
        }
        
        /* 
         * Replace orignal "User-Agent:", "Accept:", "Accept-Encoding:", 
         * "Proxy-Connection:" and "Connection:" headers; keep "Host:" and 
         * other headers
         */
        if (strstr(buf, "Host: ")) {
            printf("%s", buf);
            strcat(new_request_buf, buf);   /* Keep original host header */
            has_host_hdr = 1;

            ptr = buf + strlen("Host: ");
            strcpy(host, ptr);
            if ((ptr = strstr(host, "\r\n"))) *ptr = '\0';

            /* Separate the hostname and hostport */
            if (separate_host_port(host, hostname, &port) == -1) {
                close_fd(&serverfd, clientfd, thread_id);
                return NULL;
            }
        } else if (strstr(buf, "User-Agent: ")) {
            printf("%s", buf);              /* Print and discard */
        } else if (strstr(buf, "Accept: ")) {
            printf("%s", buf);              /* Print and discard */
        } else if (strstr(buf, "Accept-Encoding: ")) {
            printf("%s", buf);              /* Print and discard */
        } else if (strstr(buf, "Proxy-Connection: ")) {
            printf("%s", buf);              /* Print and discard */
        } else if (strstr(buf, "Connection: ")) {
            printf("%s", buf);              /* Print and discard */
        } else {
            printf("%s", buf);
            strcat(new_request_buf, buf);   /* Keep orther original headers */
        }
    }
    /* Supply a host header if it didn't exist in the orignal request */
    if (!has_host_hdr) {
        /* If no host header and no host found in the URI */
        if (!strcmp(host, "\0")) {
            close_fd(&serverfd, clientfd, thread_id);
            return NULL;
        }
        strcat(new_request_buf, "Host: ");
        strcat(new_request_buf, host);
        strcat(new_request_buf, "\r\n");
    }
    /* Compulsorily use the following headers */
    strcat(new_request_buf, user_agent_hdr);
    strcat(new_request_buf, accept_hdr);
    strcat(new_request_buf, accept_encoding_hdr);
    strcat(new_request_buf, connection_hdr);
    strcat(new_request_buf, proxy_conn_hdr);
    strcat(new_request_buf, "\r\n");
    
    /* Finished reasembling the HTTP request */
}

int forward_request_to_server(rio_t *rio_server, int *serverfd, 
        int clientfd, char *host, char *hostname, int port, 
        char *new_request_buf) 
{
    /* Open a client socket with the server */
    if ((*serverfd = Open_clientfd_r(hostname, port)) < 0) {
        if (*serverfd == -2) {
            printf("DNS error! ");
            clienterror(clientfd, host, "400", "Bad Request",
            "This webpage is not available, because DNS lookup failed");
        }
        else {
            printf("To-server socket connection error!\n");
        }
        printf("Hostname: %s\tPort: %d\n", hostname, port);
        return -1;
    }
    Rio_readinitb(rio_server, *serverfd);   /* Safe to call */

    printf("New request:\n");
    printf("%s", new_request_buf);    /* Print the new HTTP request */

    /* Forward the request to server */
    if (Rio_writen(*serverfd, new_request_buf, strlen(new_request_buf)) 
            == -1) 
    {
        return -1;
    }

    printf("{ Forwarded request to server. Ready to read response. }\n");
    return 0;
}

int cache_and_forward_response(rio_t *rio_server, int clientfd, 
    char *usrbuf, char *uri, unsigned int *byte_count) 
{
    int k = 0;
    unsigned int cnt = 0;

    while ((k = Rio_readnb(rio_server, usrbuf, MAX_OBJECT_SIZE)) > 0) {
        /* If the total response length fits in object size limit */ 
        if (cnt == 0 && k < MAX_OBJECT_SIZE) {
            /* Insert into cache */
            add_cache_item(&cache_list, uri, usrbuf, k);
        }

        if (Rio_writen(clientfd, usrbuf, k) == -1) return -1;
        byte_count += k;
        cnt++;
        /* Use printf("%s", usrbuf); here to print the response content */
    }
    if (k < 0) {
        return -1;
    }
    else {
        return 0;
    }
}



/* Parse the host string into hostname and hostport fields */
int separate_host_port(char *host, char *hostname, int *hostport) {
    char *colon;
    strcpy(hostname, host);
    if ((colon = strstr(hostname, ":"))) {
        if ((*hostport = atoi(colon+1)) == 0) {
            printf("\t(Illegal host!)\n");
            return -1;
        }
         *colon = '\0';
    } 
    else {
        *hostport = DEFAULT_PORT;   /* Use default port number */
    }
    printf("\t(Hostname: %s\tPort: %d)\n", 
            (strcmp(hostname, "\0")) ? hostname : "[Null]", *hostport);
    return 0;
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, 
        char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The proxy server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    sprintf(buf, "%sContent-type: text/html\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)strlen(body));
    sprintf(buf, "%s%s", buf, body);
    Rio_writen(fd, buf, strlen(buf));
}

/* Close any opened file descriptors (connections) in the current thread */
void close_fd(int *serverfd, int *clientfd, int thread_id) {
    if (*serverfd >= 0) Close(*serverfd);
    if (*clientfd >= 0) Close(*clientfd);

    Pthread_mutex_lock(&thread_count_mutex);
    thread_count--;
    Pthread_mutex_unlock(&thread_count_mutex);

    printf("{ [%d] Connection closed. \tCurrent Background threads: %d }\n\n", 
            thread_id, thread_count);
}

/******************************************
 * Wrappers for the pthread_mutex_* functions 
 ******************************************/

int Pthread_mutex_init(pthread_mutex_t *mutex,
        const pthread_mutexattr_t *attr)
{
    int i = pthread_mutex_init(mutex, attr);
    if (i != 0) unix_error("Pthread_mutex_init error");
    return i;
}

int Pthread_mutex_lock(pthread_mutex_t *mutex) {
    int i = pthread_mutex_lock(mutex);
    if (i != 0) unix_error("Pthread_mutex_lock error");
    return i;
}

int Pthread_mutex_unlock(pthread_mutex_t *mutex) {
    int i = pthread_mutex_unlock(mutex);
    if (i != 0) unix_error("Pthread_mutex_unlock error");
    return i;
}