/*
 * proxy.c
 * An awesome http GET request proxy.
 *
 * Andy Wang: andiwang
 * Peter Xia: fx
 *
 * Spring 2014
 * 15-213 ProxyLab
 * Carnegie Mellon University
 *
 * This is a multi-threading proxy with a cache. 
 */

#include <stdio.h>
#include "csapp.h"
#include "truck.h"
#include "contracts.h"

/* Recommended max cache and object sizes */
#define MAX_LINE 1024
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static const char *host_hdr = "Host: %s\r\n";
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *get_request_hdr = "GET %s HTTP/1.0\r\n";

static const char *host_key = "Host:";
static const char *user_agent_key = "User-Agent:";
static const char *accept_key = "Accept:";
static const char *accept_encoding_key = "Accept-Encoding";
static const char *connection_key = "Connection";
static const char *proxy_connection_key = "Proxy-Connection";

static const char *not_found_page = "<html><body>404 Not Found - ProxyLab</body></html>\r\n";


int parse_uri(char *uri, char *host, int *port, char *suffix);
void *thread_client(void *vargp);

/* Global pointer to cache */
struct cache_header *C;

/*
 * parse_uri - parse a uri http://<host>:<port(optional)><filename>. If the
 * port part is missing, a default 80 is returned.
 */
int parse_uri(char *uri, char *host, int *port, char *suffix)
{
  char uricpy[MAXLINE];
  strcpy(uricpy, uri);
  *port = 80;

  char *ptr = uricpy + 7;
  while (*ptr != '/' && *ptr != ':') {
    *host = *ptr;
    ptr++;
    host++;
  }
  *host = '\0';

  if (*ptr == ':') {
    *ptr = '\0';
    ptr++;
    sscanf(ptr, "%d%s", port, ptr);
  }

  strcpy(suffix, ptr);

  return 0;
}

/*
 * create_std_headers - construct a header with default values
 */
inline static void create_std_headers (char *std_header_buf) {
    sprintf(std_header_buf, "%s%s%s%s%s\r\n",
        user_agent_hdr,
        accept_hdr,
        accept_encoding_hdr,
        connection_hdr,
        proxy_connection_hdr);
    return;
}

/*
 * start_with - return 1 if str does not start with the string start, and
 * non-zero otherwise.
 */ 
inline static int not_start_with (char *str, const char *start) {
    return strncmp(str, start, strlen(start));
}

/*
 * create_headers_to_server - construct a header with client header
 * entries included.
 */
inline static void create_headers_to_server (rio_t *clientriop,
    char *to_server_buf, char *host, char *suffix) {

    char get_request_buf[MAXLINE];
    sprintf(get_request_buf, get_request_hdr, suffix);

    char std_hdr_buf[MAXLINE];
    create_std_headers(std_hdr_buf);

    char add_hdr_buf[MAXLINE];
    char host_buf[MAXLINE];
    strcpy(host_buf, ""); // initialize

    char buf[MAXLINE];

    while (Rio_readlineb(clientriop, buf, MAXLINE) > 0) {
        if (!strcmp(buf, "\r\n")) {
            break;
        } else if (!strncmp(buf, host_key, strlen(host_key))) {
            strcpy(host_buf, buf);
        } else if (not_start_with(buf, user_agent_key) &&
                   not_start_with(buf, accept_key) &&
                   not_start_with(buf, accept_encoding_key) &&
                   not_start_with(buf, connection_key) &&
                   not_start_with(buf, proxy_connection_key)) {
            strcat(add_hdr_buf, buf);
        }
    }

    /* if Host is not mentioned by the client */
    if (strlen(host_buf) == 0) {
        sprintf(host_buf, host_hdr, host);
    }

    /* merge headers */
    sprintf(to_server_buf, "%s%s%s%s",
                get_request_buf,
                host_buf,
                std_hdr_buf,
                add_hdr_buf);
    return;
}

/*
 * main - main routine.
 */
int main(int argc, char *argv[]) {
    int listenfd, *clientfd;
    int listenport;
    size_t clientlen;
    struct sockaddr_in clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    C = cache_init();

    listenport = atoi(argv[1]);

    if((listenfd = open_listenfd(listenport)) < 0) {
        fprintf(stderr, "failed to listen to port %s\n", argv[1]);
        exit(1);
    }

    clientlen = sizeof(clientaddr);

    while (1) {
        clientfd = Malloc(sizeof(int));
        *clientfd = Accept(listenfd, (SA *)&clientaddr,
            (socklen_t *)&clientlen);

        /* run client connection on another thread. */
        Pthread_create(&tid, NULL, thread_client, (void*)clientfd);
    }
    return 0;
}

/*
 * thread_client - thread routine. Forward request from client to server,
 * then forward data from server back to client.
 */
void *thread_client(void *vargp) {
    int clientfd = *(int *)vargp;
    Pthread_detach(pthread_self());
    free(vargp);


    rio_t clientrio, serverrio;
    char buf[MAXLINE];
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    int serverport;
    int serverfd;
    char host[MAXLINE], suffix[MAXLINE];
    char object_buf[MAX_OBJECT_SIZE];

    /* Read method, uri, version + other key:value pairs from client */
    Rio_readinitb(&clientrio, clientfd);
    Rio_readlineb(&clientrio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);


    /* If the request method is GET */
    if (!strcmp(method, "GET")) {

        struct cache_block *block = cache_find(C, uri);
        /* found in cache */
        if (block != NULL) {
            Rio_writen(clientfd, block->object, block->object_size);
        }
        /* not in cache */
        else {
            parse_uri(uri, host, &serverport, suffix);

            /* prepare for the headers */
            char to_server_buf[MAXLINE];
            create_headers_to_server(&clientrio, to_server_buf, host, suffix);

            /* Send to server */
            if ((serverfd = open_clientfd_r(host, serverport)) < 0) {
                Rio_writen(clientfd,
                    (void *) not_found_page, strlen(not_found_page));
                Close(clientfd);
                return NULL;
            }
            Rio_readinitb(&serverrio, serverfd);
            Rio_writen(serverfd, to_server_buf, strlen(to_server_buf));

            /* Get from server and send to client */
            size_t object_size = 0;
            size_t buflen;
            int cache_insert_flag = 1;

            while((buflen = Rio_readlineb(&serverrio, buf, MAXLINE)) != 0){
                Rio_writen(clientfd, buf, buflen);
                /* if the object is too big, do not insert in cache */
                if (object_size + buflen > MAX_OBJECT_SIZE) {
                    cache_insert_flag = 0;
                /* otherwise, insert */
                } else {
                    memcpy(object_buf + object_size, buf, buflen);
                    object_size += buflen;
                }
            }

            /* add to cache */
            if (cache_insert_flag) {
                cache_insert (C, uri, object_buf, object_size);
            }

            /* clear the buffer */
            Close(serverfd);
        }
    }

    Close(clientfd);
    return NULL;
}
