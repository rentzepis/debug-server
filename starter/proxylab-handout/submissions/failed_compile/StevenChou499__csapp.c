#include <stdio.h>
#include <stdbool.h>
#include "csapp.h"
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct {
    char  file_name[16];
    char *file_start;
    int   file_cached;
    int   file_size;
} file_attr;

typedef struct {
    sem_t     cache_semaphore;
    file_attr cached_file[10];
    int       victim_file;
    char      data_cache[MAX_CACHE_SIZE];
} proxy_cache;

proxy_cache http_cache;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Initialize http_cache and its semaphore */
void http_cache_init(proxy_cache *cache);
/* Parse the client request content */
void parse_cli_req(int clientfd);
/* Read all the http request headers */
void read_req_headers(rio_t *rp);
/* Parse the client's uri */
void parse_uri(char *uri, char *version, char *filename, int *server_port);
/* Make connection to server and get http content */
void connect_server(int port_num, char *filename, char *buf, int clientfd);
/* Add a recent file into the cache */
void add_to_file_cache(char *filename, char *local, int file_size);

int main(int argc, char *argv[])
{
    // If the user enter less than 2 arguments, reminds the user to enter
    if (argc != 2) {
        fprintf(stderr, "usage %s <port number>", argv[0]);
        exit(0);
    }

    // Initial variables
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    http_cache_init(&http_cache);
    
    // Receive connection from client
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // parse client http request
        pthread_create(&tid, NULL, parse_cli_req, (void *)connfd);
    }
    return 0;
}

void http_cache_init(proxy_cache *cache)
{
    // zeroing all the buffer and file names
    memset(cache->cached_file, NULL, 4 * sizeof(file_attr));
    memset(cache->data_cache, NULL, sizeof(char) * MAX_CACHE_SIZE);
    cache->victim_file = 0;
    for (int i = 0; i < 10; i++) {
        cache->cached_file[i].file_start = &cache->data_cache[i * MAX_OBJECT_SIZE];
    }

    // Initialize semaphore
    sem_init(&cache->cache_semaphore, 0, 1);
    return;
}

void parse_cli_req(int clientfd)
{
    pthread_detach(pthread_self());
    char http_buf[MAXLINE], method[128], uri[MAXLINE], version[32];
    char filename[MAXLINE];
    int server_port;
    rio_t rio;

    /* Read client request line and headers */
    Rio_readinitb(&rio, clientfd);
    if (!Rio_readlineb(&rio, http_buf, MAXLINE))
        return;
    printf("%s", http_buf);
    sscanf(http_buf, "%s %s %s", method, uri, version);
    if (strcasecmp("GET", method)) {
        fprintf(stderr, "Not a GET HTTP request\n");
        return;
    }
    read_req_headers(&rio);

    /* Parse client uri */
    parse_uri(uri, version, filename, &server_port);
    // check if the requested file has been cached
    for (int i = 0; i < 10; i++) {
        if (strcmp(http_cache.cached_file[i].file_name, filename) == 0) { // file cached
            printf("Getting file %s straight from cache index %d...\n", filename, i);
            printf("Cached file status : %d bytes\n", http_cache.cached_file[i].file_size);
            Rio_writen(clientfd, 
                       http_cache.cached_file[i].file_start, 
                       http_cache.cached_file[i].file_size);
            Close(clientfd);
            return;
        }
    }
    connect_server(server_port, filename, http_buf, clientfd);

    Close(clientfd);
    return;
}

void read_req_headers(rio_t *rp)
{
    char header_buf[MAXLINE];

    Rio_readlineb(rp, header_buf, MAXLINE);
    printf("%s", header_buf);
    while (strcmp(header_buf, "\r\n")) {
        Rio_readlineb(rp, header_buf, MAXLINE);
        printf("%s", header_buf);
    }
    return;
}

void parse_uri(char *uri, char *version, char *filename, int *server_port)
{
    char *uri_ptr;
    char *http_uri = "http://";
    char *http_version = "HTTP/1.0";

    /* Change http version to 1.0 */
    if (strncmp(version, http_version, strlen(http_version)))
        strncpy(version, http_version, strlen(http_version));
    
    /* Parse the uri and get the corresponding filename */
    if ((uri = strstr(uri, http_uri)) == NULL) {
        fprintf(stderr, "This isn't a http uri.\n");
        return;
    }
    uri += strlen(http_uri);

    if ((uri_ptr = strstr(uri, ":")) == NULL) {
        // dedicated port not found, choose port 80
        *server_port = 80;
        uri = strstr(uri, "/");
        sscanf(uri, "%s", filename);
    } else {
        // use dedicated port number
        uri_ptr += 1;
        sscanf(uri_ptr, "%d%s", server_port, filename);
    }
    printf("Choosing server by port %d with file name : %s\n\n", 
           *server_port, filename);
    return;
}

void connect_server(int port_num, char *filename, char *buf, int clientfd)
{
    char serv_port[16];
    char local_cache[MAX_OBJECT_SIZE];
    char *cache_ptr = local_cache;
    rio_t rio;

    sprintf(serv_port, "%d", port_num);
    int serverfd = Open_clientfd("localhost", serv_port);
    sprintf(buf, "GET %s HTTP/1.0\r\n", filename);
    Rio_writen(serverfd, buf, strlen(buf));
    sprintf(buf, "Host: localhost:%d\r\n", port_num);
    Rio_writen(serverfd, buf, strlen(buf));
    sprintf(buf, "\r\n");
    Rio_writen(serverfd, buf, strlen(buf));
    
    Rio_readinitb(&rio, serverfd);
    int recv_size = 0, total_size = 0;
    while ((recv_size = Rio_readnb(&rio, buf, MAXLINE)) > 0) {
        total_size += recv_size;
        if (total_size <= MAX_OBJECT_SIZE) {
            memcpy(cache_ptr, buf, recv_size);
            cache_ptr += recv_size;
        }
        Rio_writen(clientfd, buf, recv_size);
        printf("%s", buf);
    }
    printf("\n\nSending total size: %d bytes\n", total_size);
    if (total_size <= MAX_OBJECT_SIZE) {
        printf("Add file %s to cache...\n", filename);
        add_to_file_cache(filename, local_cache, total_size);
    }

    // Disconnect the server
    Close(serverfd);
    return;
}

void add_to_file_cache(char *filename, char *local, int file_size)
{
    P(&http_cache.cache_semaphore);
    bool cached = false;
    for (int i = 0; i < 10; i++) {
        if (http_cache.cached_file[i].file_cached == 0) { // file not cached
            printf("Add file %s to cache index %d\n", filename, i);
            memcpy(http_cache.cached_file[i].file_start, local, file_size);
            strncpy(http_cache.cached_file[i].file_name, filename, strlen(filename));
            http_cache.cached_file[i].file_size = file_size;
            http_cache.cached_file[i].file_cached = 1;
            cached = true;
            break;
        }
    }
    int v_i = http_cache.victim_file;
    if (cached == false) { // all index are used, choose a victim
        memcpy(http_cache.cached_file[v_i].file_start, local, file_size);
        strncpy(http_cache.cached_file[v_i].file_name, filename, strlen(filename));
        http_cache.cached_file[v_i].file_size = file_size;
        http_cache.cached_file[v_i].file_cached = 1;
        http_cache.victim_file = (v_i + 1) % 10;
    }
    V(&http_cache.cache_semaphore);
    return;
}