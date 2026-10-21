/**
 * @file proxy.c
 * @brief A multithreaded proxy which caches requests to speed up common similar requests
 * 
 * The proxy listens on the specified port for client request. Upon request the proxy checks the 
 * cache for the response. If the request is in the cache the proxy forwards it to the client and
 * never contacts the server. If the request is not in the cache the proxy contacts the server and 
 * caches the result. 
 * 
 * Every time a client connects a new thread is spawned so multiple client requests can 
 * be dealt with simultaniously. All clients share the same cache.
 * 
 * The desired port is to be specified on the command line with no other arguments
 *
 *
 * @author Jack Stellwagen <jstellwa@andrew.cmu.edu>
 **/


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



#define HOSTLEN 256
#define SERVLEN 8

typedef struct sockaddr SA;

typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;


static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1";

//Ignores sigpipe signal
void sigpipe_handler(int sig) {
    return;
}

/**
 * @brief Sends an error message to the client.
 * 
 * Takes the clients file descrriptor, the error number and the
 * desired message
 *
 */
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

/**
 * @brief Reads the headers from the rio_t
 * 
 * Will fail if the first header is not the REQUEST header 
 * and if the request method is not GET.
 * 
 * All other headers get fed to the parser to later be used
 *
 * @return 1 upon success , 0 on failure
 *
 */
int parse_headers(rio_t *rio, parser_t *parser, client_info *client) {
    char buf[MAXLINE];

    rio_readlineb(rio, buf, MAXLINE);

    parser_state state = parser_parse_line(parser, buf);

    if (state != REQUEST) {
        clienterror(client->connfd, "400", "Bad Request",
                    "Error parsing request ");

        return 0;
    }
    const char *method;

    parser_retrieve(parser, METHOD, &method);

    if (strncmp(method, "GET", MAXLINE) != 0) {
        clienterror(client->connfd, "501", "Not Implemented",
                    "proxy does not implement this method");
        return 0;
    }

    while (strncmp(buf, "\r\n", MAXLINE)) {
        rio_readlineb(rio, buf, MAXLINE);
        parser_parse_line(parser, buf);
    }

    return 1;
}

/**
 * @brief Sends the messages sent from the server to the client
 * 
 * The data sent is saved in the cache for later.
 * 
 * If the data is larger than the maximum cache size, it is not saved
 */
void forward_to_client(rio_t *server_rio, client_info *client, 
                        const char* host,  const char* port,  const char* path) {
    char buf[MAXLINE];
    char payload[MAX_OBJECT_SIZE];

    size_t n;
    size_t mem_written = 0;
    while ((n = rio_readnb(server_rio, buf, MAXLINE)) != 0) {
        rio_writen(client->connfd, buf, n);

        if (mem_written + n <= MAX_OBJECT_SIZE){
            memcpy(payload + mem_written, buf, n);
        }
        mem_written += n;
    }

    if (mem_written <= MAX_OBJECT_SIZE){
        pthread_mutex_lock(&cache_lock);
        insert(path,port,host,payload,mem_written);
        pthread_mutex_unlock(&cache_lock);
    }

}
/**
 * @brief  Forward the headers recieved from the client to the server
 * along will a few extra headers
 *
 * Forward the same Connection, Proxy-Connection, and User-Agent headers
 * independent of what the client sends.
 * 
 * Takes the server file address and the parser to read the client headers from.
 */
void forward_headers(parser_t *parser, int fd) {
    const char *host, *server_path;

    header_t *header;
    char header_buf[MAXLINE];

    parser_retrieve(parser, PATH, &server_path);
    parser_retrieve(parser, HOST, &host);

    snprintf(header_buf,MAXLINE ,"GET %s HTTP/1.0\r\n", server_path);
    rio_writen(fd, header_buf, strlen(header_buf));

    while ((header = parser_retrieve_next_header(parser)) != NULL) {
        bool replace = !strncmp(header->name, "Connection", MAXLINE) ||
                       !strncmp(header->name, "Proxy-Connection", MAXLINE) ||
                       !strncmp(header->name, "User-Agent", MAXLINE) ||
                       !strncmp(header->name, "Host", MAXLINE);

        if (!replace) {
            snprintf(header_buf, MAXLINE,"%s: %s\r\n", header->name, header->value);
            rio_writen(fd, header_buf, strlen(header_buf));

        } else if (!strncmp(header->name, "Host", MAXLINE)) {
            host = header->value;
        }
    }
    // Since we form the string with snprintf we know header_buf is null terminated
    // and strlen is safe. Same goes for above. All strings can only write to allocated 
    // space even if not null terminated
    snprintf(header_buf,MAXLINE, "Host: %s\r\n", host);
    rio_writen(fd, header_buf, strlen(header_buf));

    snprintf(header_buf,MAXLINE, "User-Agent: %s\r\n", header_user_agent);
    rio_writen(fd, header_buf, strlen(header_buf));

    rio_writen(fd, "Connection: close\r\n", strlen("Connection: close\r\n"));
    rio_writen(fd, "Proxy-Connection: close\r\n",
               strlen("Proxy-Connection: close\r\n"));
    rio_writen(fd, "\r\n", strlen("\r\n"));
}

/**
 * @brief The main function of the proxy process
 *
 * Takes a client_info struct already populated with a file descriptor.
 * 
 * 
 * Calls the functions to parse the client request, determine if the request is
 * cached in which case forward it to the client and return, if not cached contact 
 * the server and forward the response to the client. 
 * 
 * returns prematurely if headers are invaid or open_clientfd fails
 * for the given port and hostname.
 * 
 * The server will not be contacted if the request is cached.
 *
 */
void serve(client_info *client) {
    parser_t *parser = parser_new();
    int res = getnameinfo((SA *)&client->addr, client->addrlen, client->host,
                          sizeof(client->host), client->serv,
                          sizeof(client->serv), 0);
    if (res == 0) {
        printf("Accepted connection from %s:%s\n", client->host, client->serv);
    } else {
        fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
    }

    rio_t client_rio, server_rio;

    rio_readinitb(&client_rio, client->connfd);

    if (!parse_headers(&client_rio, parser, client)){
         parser_free(parser);
        return;
    }

    const char *server_port, *server_hostname, *server_path;

    parser_retrieve(parser, HOST, &server_hostname);
    parser_retrieve(parser, PORT, &server_port);
    parser_retrieve(parser, PATH, &server_path);

    cache_block_t *block;

    pthread_mutex_lock(&cache_lock);
    if (in_cache(server_hostname, server_port, server_path, &block) ){
        pthread_mutex_unlock(&cache_lock);

        rio_writen(client->connfd,block->payload,block->size);
        decrement_ref(block);

        parser_free(parser);
        return;
    }
    pthread_mutex_unlock(&cache_lock);

    int serverfd = open_clientfd(server_hostname, server_port);

    if (serverfd < 0){
         parser_free(parser);
        return;
    }

    rio_readinitb(&server_rio, serverfd);

    forward_headers(parser, serverfd);

    forward_to_client(&server_rio, client,server_hostname, server_port,server_path );

    close(serverfd);
    parser_free(parser);
}


/**
 * @brief Defines the process of a single thread.
 * 
 * Takes a pointer to a client_info struct
 * The thread calls serve on the client_info struct 
 * then cleans up resources before exiting.
 *
 */
void *thread(void *vargp) {

    client_info *client = (client_info *)vargp;

    pthread_detach(pthread_self());
    serve(client);
    close(client->connfd);
    free(client);
    return NULL;
}


/**
 * @brief Listens on the port specified by the command line argument.
 *  When a client is reached their request will be dealt with by a spawned
 *  thread. The server response will be cached for future clients aswell.
 * 
 * If more command line arguments are specified the program will exit.
 * 
 * If the port is invalid or open_listenfd otherwise fails the program will exit.
 *
 * Will continually run, listening on the specified port until killed.
 * 
 */

int main(int argc, char **argv) {
    int listenfd;
    Signal(SIGPIPE, sigpipe_handler);
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = open_listenfd(argv[1]);

    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }
    init_cache();
    while (1) {
        client_info *client = malloc(sizeof(client_info));

        /* Initialize the length of the address */
        client->addrlen = sizeof(client->addr);

        /* accept() will block until a client connects to the port */
        client->connfd =
            accept(listenfd, (SA *)&client->addr, &client->addrlen);
        if (client->connfd < 0) {
            perror("accept");
            free(client);
            continue;
        }

        pthread_create(&tid, NULL, thread, client);
    }
    // This will never be reached but the cache will never be freed since it should only 
    // be freed upon proxy exit and the proxy will not exit unless killed.
    free_cache();

    return 0;
}
