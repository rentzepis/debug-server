#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAX_HEADER_SIZE 8192
#ifndef DEBUG
#define debug_printf(...) {}
#else
#define debug_printf(...) printf(__VA_ARGS__)
#endif

pthread_rwlock_t lock;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding\r\n";
static const char *connection_hdr = "Connection\r\n";
static const char *proxy_hdr = "Proxy-connection\r\n";

// Read from the client and form a header to send to the requested server
void get_request_header(int clientfd, rio_t *client, char *header);

// make a GEt request of the client to the requested server
int GET_request(char *host, char *path, int port, char *unparsed, int *serverfd, rio_t *server, int connfd);

// take a client connection file descriptor and handle their request
void service_request(int connfd);

// function call immediately after thread creation
void *porxy_thread(void *vargp);

// read the firt line sent by the client and set the host, path, port
int parse_input(char *buf, char* host, char *path, int *port);

// Response from the server  back to client
void respond_to_client(rio_t *server, int serverfd, int clientfd, char *cache_key);

// a wrapper for rio_readlineb that close a thread on an error
int p_Rio_readlineb(int serverfd, int clientfd, rio_t *conn, char *buff, size_t size);

// a wrpper for rio_written that close a thread on an error
void p_Rio_writen(int serverfd, int clientfd, const char *buf, size_t len);

// reader lock for the cache to allow multiple readers that access the cache
// readers have priority over writers and block them
void cache_r_lock();
void cache_r_unlock();

// writer lock for the cache to allow single writer to alter the cache
// all other readers and writers are blocked
void cache_w_lock();
void cache_w_unlock();


int read_cnt;
sem_t mutex, w;
cache* p_cache;

void close_wrapper(int fd) {
    if (close(fd) < 0)
        printf("Error closing file.\n");
}  

int main(int argc, char *argv[])
{
    int listenfd, connfd, *clientfd, port, clientlen;
    struct sockaddr_in clientaddr;

    // ignore sigpipes
    signal(SIGPIPE, SIG_IGN);

    // initialize reader/writer lock for cache
    pthread_rwlock_init(&lock, NULL);

    // establish client port (default: 29094)
    if (!argv[1]){
        printf("Missing command line port number\n");
        return -1;
    }

    port = atoi(argv[1]);
    // establish listening file
    listenfd = open_listenfd(port);
    
    // initialize semaphores to 1
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
    read_cnt = 0;

    p_cache = cache_new();


    if (listenfd < 0)
        printf("open_listenfd failed.\n");
    else {
        while (1) {
            // when a client connects, spawn a new thread to handle it.
            pthread_t tid;
            clientfd = Malloc(sizeof(int));

            clientlen = sizeof(clientaddr);
            connfd = Accept(listenfd, (SA *) &clientaddr, (socklen_t *)&clientlen);
            *clientfd = connfd;
            if (clientlen < 0) {
                printf("Accept failed.\n");
                Free(clientfd);
            }
            else {
                // do something
                Pthread_create(&tid, NULL, porxy_thread, (void *)clientfd);
            }
        }

    }
    cache_free(p_cache);
    close_wrapper(listenfd);
    pthread_rwlock_destroy(&lock);
    return 0;
}

void *porxy_thread(void *vargp){
    Pthread_detach(Pthread_self());

    int fd = *(int *)vargp;

    // done with fd, free the memory allocated in main
    Free(vargp);
    service_request(fd);
    Close(fd);
    return NULL;

}

void service_request(int clientfd){
    char buf[MAXLINE];
    char cache_key[MAXLINE];
    char host[MAXLINE];
    char path[MAXLINE];
    char *header;
    int port;
    int serverfd;
    rio_t client;
    rio_t server;
    char *error = "Error 404 Not Found";
    object* cache_obj;
    int cache_hit = 0;

    // Initialize the request entry
    buf[0] = '\0';
    path[0] = '\0';
    host[0] = '\0';
    port = 80;

    // Initialize Rio reading
    Rio_readinitb(&client, clientfd);

    // Store the first line in client input to buffer
    p_Rio_readlineb(0, clientfd, &client, buf, MAXLINE);

    if (parse_input(buf, host  , path, &port) != 0)
        return;

    // Create a key for cache lookup
    sprintf(cache_key, "%s %s", host, path);
    header = malloc(MAX_HEADER_SIZE);
    bzero(header, MAX_HEADER_SIZE);
    get_request_header(clientfd, &client, header);

    cache_r_lock();
    cache_obj = cache_lookup(p_cache, cache_key);
    if (cache_obj != NULL)
        cache_hit = 1;
    cache_r_unlock();

    // When the cache is hit
    if (cache_hit){
        cache_r_lock();
        rio_writen(clientfd, (void *)cache_obj -> data, cache_obj -> size);
        cache_r_unlock();

        cache_w_lock();
        cache_update(p_cache, cache_obj);
        cache_w_unlock();

    }else{

        if (GET_request(host, path, port, header, &serverfd, &server, clientfd) != 0){
            //faill to connet the server
            rio_writen(clientfd, error, strlen(error));
            Free(header);
            return;
        }

        respond_to_client(&server, serverfd, clientfd, cache_key);
        Close(serverfd);
    }
    free(header);
    return;

}

void cache_r_lock(){
    P(&mutex);
    read_cnt += 1;
    if (read_cnt == 1)
        P(&w);
    V(&mutex);
    return;
}

void cache_r_unlock(){
    P(&mutex);
    read_cnt -= 1;
    if(read_cnt == 0)
        V(&w);
    V(&mutex);
    return;
}


void inline cache_w_lock(){
    P(&w);
}


void inline cache_w_unlock(){
    V(&w);
}

void respond_to_client(rio_t *server, int serverfd, int clientfd, char *cache_key){
    char buf[MAXLINE];
    char cache_data[MAX_OBJECT_SIZE];
    char *ptr = cache_data;
    int offset = 0;
    int byte = 0;

    bzero(buf, MAXLINE);

    // Read eh data from the server
    while ((byte = rio_readnb(server, buf, MAXLINE)) > 0){
        p_Rio_writen(clientfd, serverfd, buf, byte);

        // Try to save the data for cache
        if (offset + byte < MAX_OBJECT_SIZE)
            memcpy(ptr + offset, buf, byte);
        offset += byte;
        bzero(buf, MAXLINE);
    }

    // Fail reading from server
    if (byte == -1)
        return;

    // Cache the data received from the server
    if (offset < MAX_OBJECT_SIZE && offset > 0){
        cache_w_lock();
        cache_add(p_cache, cache_key, cache_data, offset);
        cache_w_unlock();
    }
    return;
}

void get_request_header(int clientfd, rio_t *client, char *header){
    int byte;
    int total_byte = 0;
    char buf[MAXLINE];

    while ((byte = p_Rio_readlineb(0, clientfd, client, buf, MAXLINE))){
        if(buf[0] == '\r'){
            strncat(header, buf, byte);
            return;
        }

        // overwrite these fields
        if (strstr(buf, "Host: ") != NULL)
            continue;
        if (strstr(buf, "User-Agent: ") != NULL)
            continue;
        if (strstr(buf, "Accept: ") != NULL)
            continue;
        if (strstr(buf, "Connection: ") != NULL)
            continue;
        if (strstr(buf, "Proxy-connection: ") != NULL)
            continue;

        total_byte += byte;
        if (total_byte > MAX_HEADER_SIZE)
            break;

        strncat(header, buf, byte);
    }
    return;
}

int GET_request(char *host, char *path, int port, char *header, int *serverfd, rio_t *server, int connfd){
    *serverfd = open_clientfd_r(host, port);

    // Not connected to server
    if (*serverfd < 0)
        return 1;

    // Send server and edit version of the client's header
    Rio_readinitb(server, *serverfd);
    p_Rio_writen(*serverfd, connfd, "GET ", strlen("GET "));
    p_Rio_writen(*serverfd, connfd, path, strlen(path));
    p_Rio_writen(*serverfd, connfd, " HTTP/1.0\r\n", strlen(" HTTP/1.0\r\n"));
    p_Rio_writen(*serverfd, connfd, "Host: ", strlen("Host: "));
    p_Rio_writen(*serverfd, connfd, host, strlen(host));
    p_Rio_writen(*serverfd, connfd, "\r\n", strlen("\r\n"));
    p_Rio_writen(*serverfd, connfd, user_agent_hdr, strlen(user_agent_hdr));
    p_Rio_writen(*serverfd, connfd, accept_hdr, strlen(accept_hdr));
    p_Rio_writen(*serverfd, connfd, accept_encoding_hdr, strlen(accept_encoding_hdr));
    p_Rio_writen(*serverfd, connfd, connection_hdr, strlen(connection_hdr));
    p_Rio_writen(*serverfd, connfd, proxy_hdr, strlen(proxy_hdr));
    p_Rio_writen(*serverfd, connfd, header, strlen(header));
    p_Rio_writen(*serverfd, connfd, "\r\n", strlen("\r\n"));

    return 0;

}

int parse_input(char *buf, char *host, char *path, int *port){

    // Cannot handle the request
    if (strncmp(buf, "GET http://", strlen("GET http://")) != 0)
        return 1;

    int offset = strlen("GET http://");
    int i = offset;

    // Add a hostname to buf
    while (buf[i] != '\0'){
        if (buf[i] == ':')
            break;
        if (buf[i] == '/')
            break;

        host[i - offset] = buf[i];
        i += 1;
    }

    // End of the host
    if (buf[i] == ':')
        sscanf(&buf[i+1], "%d%s", port, path);
    else
        sscanf(&buf[i], "%s", path);

    host[i - offset] = '\0';
    return 0;
}

int p_Rio_readlineb(int serverfd, int clientfd, rio_t *conn, char *buf, size_t size){
    int byte;

    if((byte = rio_readlineb(conn, buf, size)) < 0){
        // Exit thread
        if (serverfd)
            close(serverfd);
        close(clientfd);
        Pthread_exit(NULL);
    }

    return byte;
}

void p_Rio_writen(int serverfd, int clientfd, const char* buf, size_t len){
    if (rio_writen(serverfd, (void *)buf, len) < 0){
        close(serverfd);
        close(clientfd);
        Pthread_exit(NULL);
    }
}


