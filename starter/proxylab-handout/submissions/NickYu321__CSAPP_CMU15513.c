/**
 * @file proxy.c
 * @brief Implementation of a simple multi-threaded proxy server.
 *
 * This file defines the main functionalities of a proxy server that handles
 * HTTP requests and responses using a simple cache system. The proxy supports
 * multi-threading, creating a new thread for each incoming client connection.
 *
 * Key features include parsing HTTP requests, forwarding requests to web servers,
 * caching responses, and serving cached content to clients when available. The
 * proxy can handle multiple concurrent connections, and each connection is
 * processed by a separate thread for improved performance.
 *
 * The cache system utilized by the proxy allows for storing frequently accessed
 * web content, optimizing the retrieval process and reducing response times.
 *
 */


/* Some useful includes to help you get started */

#include "csapp.h"
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
typedef struct sockaddr SA;
/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20230411 Firefox/63.0.1";

Cache cache;
void doit(int fd,Cache *cache);
void read_requesthdrs(rio_t *rp);
void parse_uri(char *uri, char *host, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void generate_header(char *head, char *host, char *port, char *path, rio_t *rio);
void *thread(void *vargp);
/**
 * @brief Signal handler for SIGPIPE. Ignores the signal.
 *
 * @param signo Signal number.
 *
 * This handler is set up to ignore the SIGPIPE signal, preventing the program from terminating
 * when attempting to write to a closed socket.
 */
void sigpipe_handler(int signo) {
    // This function is empty because we just want to ignore the signal.
}

/**
 * @brief Main function for the proxy program.
 *
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line argument strings.
 * @return Exit status of the program.
 *
 * This function serves as the entry point for the proxy program.
 * It initializes necessary components, sets up a listening socket, and spawns threads to handle client requests.
 */
int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];
    cache_init(&cache);
    /* Check command line args */
    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(1);
    }

    if (signal(SIGPIPE, sigpipe_handler) == SIG_ERR) {
        fprintf(stderr, "Error setting up signal handler for SIGPIPE\n");
        exit(1);
    }
    listenfd = open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (connfd < 0) {
            fprintf(stderr, "Error accepting connection\n");
            continue;  // Skip this iteration and continue with the next one
        }

        getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);

        // Create a new thread for each request
        pthread_t tid;
        int *connfd_ptr = malloc(sizeof(int));
        if (connfd_ptr == NULL) {
            fprintf(stderr, "Error allocating memory for connfd_ptr\n");
            close(connfd);  // Close the connection to avoid a leak
            continue;  // Skip this iteration and continue with the next one
        }
        *connfd_ptr = connfd;
        printf("Main thread: Created thread for connfd: %d\n", connfd);
        pthread_create(&tid, NULL, thread, connfd_ptr);
    }
    cache_deinit(&cache);
    return 0;
}
/**
 * @brief Thread function for handling client requests.
 *
 * @param vargp Pointer to the client connection file descriptor.
 * @return NULL.
 *
 * This function is executed by each thread to handle a specific client request.
 * It calls the 'doit' function to process the request and then closes the client connection.
 */
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    free(vargp); // Free the memory allocated for connfd_ptr

    printf("Thread %lu: Started for connfd: %d\n", pthread_self(), connfd);
    
    pthread_detach(pthread_self()); // Detach the thread

    doit(connfd,&cache);

    close(connfd);
    printf("Thread %lu: Closed for connfd: %d\n", pthread_self(), connfd);
    return NULL;
}

/**
 * @brief Handles one HTTP request/response transaction.
 *
 * @param fd File descriptor of the client connection.
 * @param cache Pointer to the cache structure.
 *
 * This function processes a single HTTP request and generates an appropriate response.
 * It checks if the requested object is in the cache and serves it if available.
 * If not, it fetches the object from the server, updates the cache, and sends the response to the client.
 */
void doit(int fd,Cache *cache) 
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE],path[MAXLINE],head[MAXLINE],port[MAXLINE];
    char port_buf[20];
    rio_t rio;
    rio_t rio_server;
    //int *port = NULL;

    //initialize
    rio_readinitb(&rio,fd);
    //if no input
    if(!rio_readlineb(&rio, buf, MAXLINE)) return;
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented","Proxy does not implement this method");
        return;
    }
    // Check if the requested object is in the cache
    CacheEntry *cached_object = cache_find(cache, uri);
    if (cached_object != NULL) {
        // Send the cached object to the client
        rio_writen(fd, cached_object->object, cached_object->object_size);
        return; // Return early since we've served the object from the cache
    }

    // Parse URI
    //GET http://milkshark.ics.cs.cmu.edu:28348/random-text.txt HTTP/1.0\r\n
    printf("uri:%s\n",uri);
    //http://milkshark.ics.cs.cmu.edu:28348/random-text.txt
    parse_uri(uri, host, port, path);
    generate_header(head, host, port,path,&rio);
    
    printf("%s %s %s %s %s\n", method, host, port, path, version);

    //connect to the server
    sprintf(port_buf,"%s",port);
    int client_fd = open_clientfd(host,port);
    //bad IP
    if (client_fd < 0) {
        // Handle the error by returning an error response to the client
        clienterror(fd, "Bad Gateway", "502", "Bad Gateway", "Unable to connect to the server");
        return;
    }
    
    rio_readinitb(&rio_server,client_fd);
    rio_writen(client_fd,head,strlen(head));
    // Receive the server's response
    char response_buf[MAX_OBJECT_SIZE];
    int n = 0;
    size_t total_size = 0;
    // Reply to the client
    while ((n = rio_readnb(&rio_server, buf, MAXLINE))) {
        rio_writen(fd, buf, n);
        if (total_size + n <= MAX_OBJECT_SIZE) {
            memcpy(response_buf + total_size, buf, n);
        }
        total_size += n;
    }
    // Store the server's response in the cache if appropriate
    if (total_size <= MAX_OBJECT_SIZE) {
        CacheEntry *existing_entry = cache_find(cache, uri);
        if (existing_entry == NULL) {
            cache_add(cache, uri, response_buf, total_size);
        }
        
    }
    close(client_fd);
    printf("Thread %lu: Completed request for connfd: %d\n", pthread_self(), fd);
}

/**
 * @brief Parses a URI into its components: host, port, and path.
 *
 * @param uri Input URI string.
 * @param host Output buffer for the host.
 * @param port Output buffer for the port.
 * @param path Output buffer for the path.
 *
 * This function extracts the host, port, and path components from a given URI.
 * It handles the case where the port is not specified, defaulting to port 80.
 */
void parse_uri(char *uri, char *host, char *port, char *path) {
    // Check if the URI starts with "http://"
    // Host: milkshark.ics.cs.cmu.edu
    // Port: 28348
    // Path: /random-text.txt
    if (strstr(uri, "http://") == NULL) {
        fprintf(stderr, "Error: Invalid URI format\n");
        exit(1);
    }

    // Skip "http://"
    char *start = uri + strlen("http://");

    // Find the end of the host part
    char *end_host = strpbrk(start, ":/");
    char *end_path = strchr(start, '/');

    if (end_host == NULL) {
        // Port is not specified, default to 80
        end_host = start + strlen(start);
        sprintf(port, "%d", 80);  // Convert integer 80 to string
    } else {
        if (*end_host == ':') {
            // Port is specified
            sprintf(port, "%.*s", (int)(strpbrk(end_host + 1, "/") - (end_host + 1)), end_host + 1);
        } else {
            // Port is not specified, default to 80
            sprintf(port, "%d", 80);  // Convert integer 80 to string
        }
        end_host = start + (end_host - start);
    }

    // Extract host
    strncpy(host, start, end_host - start);
    host[end_host - start] = '\0';

    // Extract path
    if (end_path != NULL) {
        strcpy(path, end_path);
    } else {
        strcpy(path, "/");
    }
}
/**
 * @brief Generates the HTTP header for the proxy request.
 *
 * @param head Output buffer for the generated header.
 * @param host Hostname for the proxy request.
 * @param port Port number for the proxy request.
 * @param path Path for the proxy request.
 * @param rio Pointer to the rio_t structure containing client request headers.
 *
 * This function generates the HTTP header for a proxy request based on the client's request headers.
 * It ensures necessary headers are present and appends default values if not provided by the client.
 */
void generate_header(char *head, char *host, char *port, char *path, rio_t *rio)
{
    char buf[MAXLINE];

    // Set default headers
    sprintf(head, "GET %s HTTP/1.0\r\n", path);

    int user_agent_present = 0;
    int connection_present = 0;
    int proxy_connection_present = 0;
    int host_info_present = 0;

    // Read and copy client's request headers
    while (rio_readlineb(rio, buf, MAXLINE) > 0)
    {
        if (strcmp(buf, "\r\n") == 0)
        {
            break;
        }
        strcat(head, buf);

        // Check for specific headers
        if (strstr(buf, "User-Agent:") != NULL)
        {
            user_agent_present = 1;
        }
        else if (strstr(buf, "Proxy-Connection:") != NULL)
        {
            proxy_connection_present = 1;
        }
        else if (strstr(buf, "Connection:") != NULL)
        {
            connection_present = 1;
        }
        else if (strstr(buf, "Host:") != NULL)
        {
            host_info_present = 1;
        }
    }

    // Add missing headers if not present
    if (!host_info_present)
    {
        sprintf(buf, "Host: %s:%s\r\n", host, port);
        strcat(head, buf);
    }

    // Append User-Agent if not present
    if (!user_agent_present)
    {
        strcat(head, header_user_agent);
    }

    // Append Connection if not present
    if (!connection_present)
    {
        strcat(head, "Connection: close\r\n");
    }

    // Append Proxy-Connection if not present
    if (!proxy_connection_present)
    {
        strcat(head, "Proxy-Connection: close\r\n");
    }

    // Add terminator for request
    strcat(head, "\r\n");
    printf("Debug: Final Header: \n%s", head);
}

/*
 * clienterror - returns an error message to the client
 * From the book
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s:%s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}