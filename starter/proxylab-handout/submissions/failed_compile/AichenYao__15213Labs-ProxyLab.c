/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"
#include "http_parser.h"
#include "cache.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
pthread_mutex_t mutex;
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20201123 Firefox/63.0.1\r\n";


/* Display an error message to the users;
 from tiny.c given it the handout */
void clienterror(int fd, const char *errnum, const char *shortmsg,
                 const char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
                       "<!DOCTYPE html>\r\n"
                       "<html>\r\n"
                       "<head><title>Tiny Error</title></head>\r\n"
                       "<body bgcolor=\"ffffff\">\r\n"
                       "<h1>%s: %s</h1>\r\n"
                       "<p>%s</p>\r\n"
                       "<hr /><em>The Tiny Web server</em>\r\n"
                       "</body></html>\r\n",
                       errnum, shortmsg, longmsg);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
                      "HTTP/1.0 %s %s\r\n"
                      "Content-Type: text/html\r\n"
                      "Content-Length: %zu\r\n\r\n",
                      errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

void doit(int client_fd) {
    // parse command lines (header or GET requests) and send to servers
    int server_fd = 0;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    strcpy(buf, "");
    strcpy(method, "");
    strcpy(uri, "");
    strcpy(version, "");
    rio_t client_rio, server_rio;
    // associate client's fd with its rio structure
    rio_readinitb(&client_rio, client_fd);
    int n;
    parser_t *new_parser = parser_new();
    char send_final[MAXLINE];
    memset(send_final, 0, MAXLINE);
    int og_header = 0;
    // send_final is the request and all headers that we are sending
    // CASE: read 0 byte from the client
    while ((n = rio_readlineb(&client_rio, buf, MAXLINE)) > 0) {
        if (!strcmp(buf, "\r\n")) {
            break;
        }
        sscanf(buf, "%s %s %s", method, uri, version);
        parser_state new_parser_state = parser_parse_line(new_parser, buf);
        if (new_parser_state == ERROR) {
            printf("parser error\n");
            parser_free(new_parser);
            return;
        }
        if (new_parser_state == REQUEST) {
            if (strcasecmp(method, "GET")) {
                // cannot implement other types of requests
                clienterror(client_fd, "501", " Not implemented",
                            "Tiny does not implement this method");
                parser_free(new_parser);
                return;
            }
            block_t *block;
            pthread_mutex_lock(&mutex);
            if ((block = cache(uri)) != NULL) {
                //increment the refcnt of the block being hit and decrement 
                //after we write that to the client
                block->refcnt += 1;
                fprintf(stderr,"cache hit: %s\n", block->value);
                rio_writen(client_fd, block->value, strlen(block->value));
                block->refcnt -= 1; 
                if (block->refcnt == 0) {
                    free_block(block);
                }
                pthread_mutex_unlock(&mutex);
                return;
            }
            pthread_mutex_unlock(&mutex);
             //if we didn't hit on a block, also unlock
            const char *hostname;
            const char *path;
            const char *port;
            int ret_num1 = parser_retrieve(new_parser, HOST, &hostname);
            if (ret_num1 == -1 || ret_num1 == -2) {
                printf("cannot parse HOST\n");
                exit(-1);
            }
            int ret_num2 = parser_retrieve(new_parser, PATH, &path);
            if (ret_num2 == -1 || ret_num2 == -2) {
                printf("cannot parse PATH\n");
                exit(-1);
            }
            int ret_num3 = parser_retrieve(new_parser, PORT, &port);
            if (ret_num3 == -1 || ret_num3 == -2) {
                printf("cannot parse PORT\n");
                exit(-1);
            }
            // establish a connection with a server to listen for requests
            // Assume the GET request is always ahead of the headers.
            server_fd = open_clientfd(hostname, port);
            if (server_fd < 0) {
                printf("connection failed\n");
                close(server_fd);
                return;
            }
            rio_readinitb(&server_rio, server_fd);
            char server_header[MAXLINE];
            sprintf(server_header, "GET %s HTTP/1.0\r\n", path);
            strncat(send_final, server_header, MAXLINE - 1);
        }
        if (new_parser_state == HEADER) {
            // Host Header
            // keep a flag for the 4 headers
            if (og_header == 0) {
                header_t *host_header_struct =
                    parser_lookup_header(new_parser, "Host");
                char host_header[MAXLINE];
                sprintf(host_header, "Host: %s\r\n",
                        (host_header_struct)->value);
                strncat(send_final, host_header, MAXLINE - 1);

                // User-Agent Header
                char ua_header[MAXLINE];
                sprintf(ua_header, "User-Agent: %s", header_user_agent);
                strncat(send_final, ua_header, MAXLINE - 1);

                // Connection Header
                char conn_header[MAXLINE];
                sprintf(conn_header, "Connection: %s", "close\r\n");
                strncat(send_final, conn_header, MAXLINE - 1);

                // Proxy-Connection Header
                char pro_header[MAXLINE];
                sprintf(pro_header, "Proxy-Connection: %s", "close\r\n");
                strncat(send_final, pro_header, MAXLINE - 1);
                og_header = 1;
            }
            // forward any other header that is not one of the four above
            header_t *new_header_struct;
            while ((new_header_struct =
                        parser_retrieve_next_header(new_parser)) != NULL) {
                char new_header[MAXLINE];
                memset(new_header, 0, MAXLINE);
                if (!strncmp((new_header_struct)->name, "Host",
                             sizeof("Host")) ||
                    !strncmp((new_header_struct)->name, "Connection",
                             sizeof("Connection")) ||
                    !strncmp((new_header_struct)->name, "Proxy-Connection",
                             sizeof("Proxy-Connection")) ||
                    !strncmp((new_header_struct)->name, "User-Agent",
                             sizeof("User-Agent"))) {
                    continue;
                }
                sprintf(new_header, "%s: %s\r\n", (new_header_struct)->name,
                        (new_header_struct)->value);
                strncat(send_final, new_header, strlen(new_header));
            }
        }
    }
    strncat(send_final, "\r\n", MAXLINE - 1);
    parser_free(new_parser);
    int writebytes = rio_writen(server_fd, send_final, strlen(send_final));
    if (writebytes == -1) {
        printf("an error with sending requests to the server\n");
        close(server_fd);
        return;
    }
    if (n == -1) {
        printf("an error has occured\n");
        close(server_fd);
        return;
    }
    // otherwise (n == 0), it has run into EOF
    char buf_new[MAXLINE];
    strcpy(buf_new, "");
    // read the response from the server
    int readbytes;
    // char value_to_add[MAXLINE];
    // strcpy(value_to_add, "");
    // int count = 0; //the total number of bytes of the server's response
    while ((readbytes = rio_readnb(&server_rio, buf_new, MAXLINE)) > 0) {
        rio_writen(client_fd, buf_new, readbytes);
        // count += readbytes;
        // if (count < MAX_OBJECT_SIZE) {
        //     memcpy((&(value_to_add[count-readbytes])), buf_new, readbytes);
        // }
        // if (count >= MAX_OBJECT_SIZE) {
        //     fprintf(stderr, "-------block too large------\n");
        // }
    }
    if (readbytes == -1) {
        close(server_fd);
        return;
    }
    // if (count < MAX_OBJECT_SIZE) {
    //     //if the server's response is less than MAX_OBJECT_SIZE,
    //     //then add this new block to the cache
    //     fprintf(stderr, "------new block added-----\n");
    //     pthread_mutex_lock(&mutex);
    //     add_new_block(uri, value_to_add);
    //     pthread_mutex_unlock(&mutex);
    // }
    close(server_fd);
    return;
}

void *thread(void *vargp) {
    int client_fd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    // each thread is responsible for cleaning up resource: the malloced int*
    doit(client_fd);
    close(client_fd);
    return NULL;
}

/*  Cite Cs:APP textbook 11.6  */
// Q: clientaddr
// Q: Shall I check for errors for each "non-wrapped" functions and exit if -1?
int main(int argc, char **argv) {
    int listen_fd, *client_fd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr clientaddr;
    pthread_mutex_init(&mutex, NULL);
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    signal(SIGPIPE, SIG_IGN);
    // simply ignore SIGPIPE because we don'w want to terminate the whole
    // program
    listen_fd = open_listenfd(argv[1]);
    if (listen_fd == -1) {
        printf("cannot establish a listening descriptor\n");
        exit(1);
    }
    while (true) {
        clientlen = sizeof(clientaddr);
        client_fd = malloc(sizeof(int));
        *client_fd = accept(listen_fd, &clientaddr, &clientlen);
        if (*client_fd == -1) {
            printf("cannot establish a connected descriptor\n");
            continue;
        }
        int n = getnameinfo(&clientaddr, clientlen, hostname, MAXLINE, port,
                            MAXLINE, 0);
        if (n != 0) {
            printf("cannot extract info from the socket address structure\n");
            continue;
        }
        sio_printf("Accepted connection from (%s, %s)\n", hostname, port);
        pthread_t tid;
        // from writeup 5.1: spawn a new worker thread for each request
        pthread_create(&tid, NULL, thread, (void *)(client_fd));
    }
    return 0;
}
