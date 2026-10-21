/* proxy.c - Proxylab by wkanchan & pthepdus */

#include <stdio.h>
#include <string.h>
#include "ptypes.h"
#include "cache.h"
#include "proxy.h"
#include "csapp.h"
// #include "proxythread.h"

static const char *msg_http_version = "HTTP/1.0";
static const char *msg_user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *msg_accept = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *msg_accept_encoding = "Accept-Encoding: gzip, deflate\r\n";
static const char *msg_connection = "Connection: close\r\n";
static const char *msg_proxy_connection = "Proxy-Connection: close\r\n";

/* main - Initialize variables, prefork worker threads,
    open listening port, accept incoming connections and add them
    to the task buffer (sbuf) */
int main(int argc, char **argv)
{
    int listenfd, browserfd, port, i;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;

    /* IMPORTANT: Initialize sbuf & cache */
    sbuf_init(&sbuf, SBUFSIZE);
    cache_init();

    /* SIGPIPE handler (Broken pipe) */
    Signal(SIGPIPE,  sigpipe_handler);

    /* If no port specified in the argument, use default (wkanchan:4647) */
	port = (argc != 2)?PORT:atoi(argv[1]);
    printf("\n\n\n\n\n\nRunning proxy at port %d..\n", port);
  
    /* Prefork worker threads into the thread pool */
    for (i = 0; i < THREAD_POOL_SIZE; i++) {
        Pthread_create(&tid[i], NULL, request_handler, NULL);
    }

    /* Listen to incoming clients, accept, and insert it to sbuf
        so that worker threads can grab and process them. */
    if ((listenfd = open_listenfd(port)) < 0) {
        fprintf(stderr, "Open_listenfd error: %s\n", strerror(errno));
        exit(0);
    }
    while (1) {
        clientlen = sizeof(clientaddr);
        browserfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        dbg_printf("Accepted. [browserfd = %d]\n", browserfd);
        sbuf_insert(&sbuf, browserfd);
    }

    /* It won't reach here. But put this as a good convention */
    sbuf_deinit(&sbuf);

    return 0;
}

/*
 * request_handler - Thread routine once proxy server accept a request.
 *      Detach itself. Then loop forever to grab a file descriptor from sbuf
 *      inserted by the main thread (that accepted a connection).
 *      Then process it. And finally close it.
 */
void *request_handler(void *vargp){
    Pthread_detach(pthread_self());

    /* Wait for the job, which is a browser's file descriptor in s_buf
        After this thread got the browserfd, it then process it, close it,
        and finally wait for the other one. Do this forever */
    while (1) {
        int browserfd = sbuf_remove(&sbuf);
        dbg_printf("[Thread %u] is handling browserfd %u\n",
                    (unsigned int)pthread_self(), browserfd);
        process_conn(browserfd);
        Close(browserfd);
        dbg_printf("[Thread %u] closed browserfd %u\n",
                    (unsigned int)pthread_self(), browserfd);
    }
    
    /* The thread never reaches here because of the while loop */
    return NULL;
}

/* 
 * process_conn - Process the connection
 *      Connect and request data from a web server.
 *      Check if there's a cache for that then serves the cached data.
 *      Otherwise, retrieve data from the web server and
 *      store it in a cache if its size doesn't exceed the limit.
 */
void process_conn(int browserfd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], /*path[MAXLINE],*/ cachebuf[MAX_OBJECT_SIZE];
    rio_t browser_rio, webserver_rio;
    int webserverfd = -1, n, is_exceeded_max_object_size;
    size_t cachebuf_size;
    int setjmp_ret, i;

    /* Landing point for error handling */
    setjmp_ret = setjmp(jmp_buf_env);
    if (setjmp_ret > 0) {
        /* Error called from longjmp */
        fprintf(stderr, "** Error called from longjmp %d\n", setjmp_ret);
        clienterror(browserfd, "GET", "500", "Server error",
            "Connection with the web server is lost.");
        // check_cache();
        /* Look for the slot of this thread in tid[] in order to replace it */
        for (i=0; i<THREAD_POOL_SIZE; i++) {
            if (tid[i] == pthread_self()) {       
                Pthread_create(&tid[i] , NULL, request_handler, NULL);         
                dbg_printf("[Thread %u] Created thread %u.\n",
                    (unsigned int)pthread_self(), (unsigned int)tid[i]);
            }
        }
        dbg_printf("[Thread %u] Closing browserfd %d..\n",
            (unsigned int)pthread_self(), browserfd);
        Close(browserfd);

        dbg_printf("[Thread %u] exiting\n",
                    (unsigned int)pthread_self());
        Pthread_exit(&i);
        return;
    }

    /* Read request line and headers, only GET method is supported */
    Rio_readinitb(&browser_rio, browserfd);
    Rio_readlineb(&browser_rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    dbg_printf("[Thread %u] %s %s %s\n", (unsigned int)pthread_self(), method,
        uri, version);
    if (strcasecmp(method, "GET")) {
        clienterror(browserfd, method, "501", "Not Implemented",
            "This proxy doesn't implement this method.");
        return;
    }

    // dbg_printf("[Thread %u] xxx 1\n", (unsigned int)pthread_self());

    /* Check if the object with this uri is existed in the cache */
    CacheObject *cacheobj = cache_get(uri);
    if (cacheobj != NULL) {
        /* Serve this cached object to the browser right away */
        Rio_writen(browserfd, cacheobj->data, cacheobj->size);
        dbg_printf("@@@@@ Served from the cache %u bytes\n", 
            (unsigned int)cacheobj->size);
        return;
    }
    // dbg_printf("[Thread %u] xxx 2\n", (unsigned int)pthread_self());

    /* Read Host value and store it */
    if (!readline_host(&browser_rio, buf, host)) {
        clienterror(browserfd, method, "500", "Host header not found", buf);
        return;
    }

    /* Connect to the server specified by "host" */
    dbg_printf("* [Thread %u] Connecting to %s..\n", 
        (unsigned int)pthread_self(), host);
    if ((webserverfd = open_clientfd_r(host, HTTP_PORT)) < 0) {
        if (webserverfd == -1)
            clienterror(browserfd, method, "500", "Server error", 
                strerror(errno));
        else {   
            char errmsg[MAXLINE];
            sprintf(errmsg, "DNS error: %d", h_errno);
            clienterror(browserfd, method, "500", "DNS error", errmsg);
        }
        return;
    }
    // dbg_printf(" connected.\n");
    Rio_readinitb(&webserver_rio, webserverfd);

    /* Send HTTP header */
    write_defaulthdrs(webserverfd, method, host, uri);

    /* Read the rest of the header and forward necessary ones */
    readwrite_requesthdrs(&browser_rio, webserverfd);

    /* Read each line of response from the web server
        Accumulate it to the cache buffer, then forward it to the browser */
    cachebuf_size = 0;
    is_exceeded_max_object_size = 0;
    while ((n = Rio_readlineb(&webserver_rio, buf, MAXLINE)) != 0) {
        /* If accumulating this makes cachebuf exceeds max size, 
            then we don't need to put this cachebuf to the cache. */
        if (!is_exceeded_max_object_size &&
            cachebuf_size + n <= MAX_OBJECT_SIZE) {
            memcpy((cachebuf + cachebuf_size), buf, n);
            cachebuf_size += n;
        } else {
            is_exceeded_max_object_size = 1;
        }

        Rio_writen(browserfd, buf, n);
    }

    /* If this cachebuf doesn't exceed the maximum size,
        then put it in the cache */
    if (!is_exceeded_max_object_size) {
        int errorcode = cache_insert(uri, cachebuf, cachebuf_size);
        if (!errorcode) {
            dbg_printf("^^^ Inserted cache %u bytes (now cache.size = %u)\n", 
                (unsigned int)cachebuf_size, (unsigned int)cache.size);
        } else {
            dbg_printf("ZZZ Couldn't insert to cache %d ZZZ\n",errorcode);
        }
    } else {
        dbg_printf("XXX Object exceeds max size. Not cached. XXX\n");
    }
    
    Close(webserverfd);
}

// /*
//  * parse_uri - Given uri, extract the path.
//  */
// void parse_uri(char *uri, char *path) {
//     char *ptr;
//     ptr = strstr(uri, "://");
//     ptr += 3;
//     ptr = strchr(ptr, '/');
//     strncpy(path, ptr, (int)strlen(uri));
// }

/* readline_host - Given a header "Host" line, 
 *      extract host from the line.
 */
int readline_host(rio_t *browser_rp, char *buf, char *host) {
    char key[MAXLINE], value[MAXLINE];

    dbg_printf("- readline_host");
    dbg_printf(" buf: %s\n", buf);

    Rio_readlineb(browser_rp, buf, MAXLINE);
    sscanf(buf, "%s %s", key, value);
    if (strcasecmp(key, "Host:")) {
        return 0;
    }
    strncpy(host, value, strlen(value));
    host[strlen(value)] = '\0';
    return 1;
}

/* write_defaulthdrs - Write headers specified in the writeup
 *      to the web server
 */
void write_defaulthdrs(int webserverfd,char *method,char *host,char *path) {
    char buf[MAXLINE];

    sprintf(buf, "%s %s %s\r\n", method, path, msg_http_version);
    Rio_writen(webserverfd, buf, strlen(buf));

    sprintf(buf, "Host: %s\r\n", host);
    Rio_writen(webserverfd, buf, strlen(buf));

    Rio_writen(webserverfd, (void *)msg_user_agent, 
        strlen(msg_user_agent));
    Rio_writen(webserverfd, (void *)msg_accept, 
        strlen(msg_accept));
    Rio_writen(webserverfd, (void *)msg_accept_encoding, 
        strlen(msg_accept_encoding));
    Rio_writen(webserverfd, (void *)msg_connection, 
        strlen(msg_connection));
    Rio_writen(webserverfd, (void *)msg_proxy_connection, 
        strlen(msg_proxy_connection));
}

/* readwrite_requesthdrs - Read the rest of the header
 *      and only forward lines that aren't specified in the writeup 
 */
void readwrite_requesthdrs(rio_t *browser_rio, int browserfd) {
    char buf[MAXLINE], key[MAXLINE];

    do {
        /* Read each line, get only key and consider forwarding it */
        Rio_readlineb(browser_rio, buf, MAXLINE);
        // dbg_printf("<< %s", buf);
        strcpy(key, "");
        sscanf(buf, "%s", key);
        if (strcasecmp(key, "User-Agent") &&
            strcasecmp(key, "Accept:") &&
            strcasecmp(key, "Accept-Encoding:") &&
            strcasecmp(key, "Connection:") &&
            strcasecmp(key, "Proxy-Connection:")) {
            Rio_writen(browserfd, (void *)buf, strlen(buf));
        }
    } while (strcmp(buf, "\r\n"));
}

/* clienterror - Print error page */
void clienterror(int browserfd, char *cause, char *errnum,
    char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<h1>%s: %s</h1>\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s</p>\r\n", body, longmsg, cause);
    sprintf(body, "%s</html>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(browserfd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(browserfd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(browserfd, buf, strlen(buf));
    Rio_writen(browserfd, body, strlen(body));

}

/* 
 * sigpipe_handler - Handle SIGPIPE .  
 */
void sigpipe_handler(int sig) 
{
    printf("!!!! [Thread %u] !! SIGPIPE !!!!\n",
        (unsigned int)pthread_self());
    // check_cache();
    longjmp(jmp_buf_env, 5000);
    return;
}