/*
 * Author: Walt Li, Eric Gassel
 */


#include "csapp.h"

#define MAXCACHEITEM 102400 /* maximum size of a cache entry ~ 100 kB */

/* suggested entry and cache structs for Part II */
typedef struct cache_entry {
    char *uri;
    char *item;
    struct cache_entry *next;
    size_t size;
} cache_entry_t;

typedef struct {
    cache_entry_t *head;
    size_t total_size;
} cache_t;

// helper functions
int parse_uri(char *uri, char *hostname, char *port, char *path);

// For Part II: a global variable to point to the in-memory cache
cache_t *cache;

/* print out the contents of the cache */
void cache_print() {
    cache_entry_t *cur = cache->head;
    printf("CACHE INFO: current cache: (%zd)\n", cache->total_size);
    fflush(stdout);
    while(cur) {
        printf("----%s (%zd)\n%s\n", cur->uri, cur->size, cur->item);
        fflush(stdout);
        cur = cur->next;
    }
    printf("--------------------------------\n\n");
    fflush(stdout);
}

/* initialize the global cache variable (allocate memory and initialize fields) */
void cache_init() {
    cache = malloc(sizeof(cache_t));
    cache->head = NULL;
    cache->total_size = 0;
}

/* deallocate the entire cache (all the entries and the cache global variable) */
void cache_free() {
    cache_entry_t *cur = cache->head;
    while (cur) {
        cache_entry_t *temp = cur;
        cur = cur->next;
        free(temp->uri);
        free(temp->item);
        free(temp);
    }
    free(cache);
}

/* search cache for an entry with a matching uri 
 * return a pointer to the matching entry or NULL if no matching entry is found
*/
cache_entry_t* cache_lookup(char *uri) {
    cache_entry_t *cur = cache->head;
    while (cur) {
        if (!strcmp(cur->uri, uri)) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

/* insert a new entry at the head of the cache */
void cache_insert(char *uri, char *item, size_t size) {
    cache->total_size += size; //add size

    // allocate space for new entry, url, and item
    cache_entry_t *entry = malloc(sizeof(cache_entry_t));
    entry->uri = malloc(strlen(uri)+1);
    entry->item = malloc(size);
    entry->size = size;

    // copy uri and item into memory
    strcpy(entry->uri, uri);
    memcpy(entry->item, item, size);

    // insert entry to head
    entry->next = cache->head;
    cache->head = entry;
}

/* Implement this function for Part I
 * For Part III, you may need to change the parameter and return type of handle_request
 */
void* handle_request(void *argvp) {
    long connfd = (long) argvp;
    pthread_detach(pthread_self()); // detach thread

    // read and parse request
    // hint: the initial part of the doit function in tiny/tiny.c may be a good starting point
    char client_buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t client_rio;

    Rio_readinitb(&client_rio, connfd);
    if (!Rio_readlineb(&client_rio, client_buf, MAXLINE)) // If no input, don't do anything
        return NULL;
    // printf("Check request: %s", client_buf);
    // fflush(stdout);
    
    // parsing input into method, uri, version
    sscanf(client_buf, "%s %s %s", method, uri, version);

    // If the method is not GET or URI not starting with http, it is a bad request
    if (strcasecmp(method, "GET") || !(strstr(uri, "http://"))) {
        fprintf(stderr, "The proxy cannot handle this request.\n");
        Close(connfd);
        return NULL;
    }

    // if uri in cache, directly send cache to client, and end the function
    cache_entry_t *cache_found = cache_lookup(uri);
    if (cache_found) {
        // printf("||||||||||||||||  Cache found!!\n");
        // fflush(stdout);
        Rio_writen(connfd, cache_found->item, cache_found->size);
        return NULL;
    }
    // printf("|||||||||||||||| Cache NOT found!!\n");
    // fflush(stdout);

    // parse URI for hostname, port, and filename
    char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
    int parse = parse_uri(uri, hostname, port, path);
    if (parse == 1) {
        fprintf(stderr, "Proxy cannot handle URIs with ':' after HOSTNAME:PORT\n");
        Close(connfd);
        return NULL;
    }

    //open a socket on that port and hostname
    rio_t rio_server;
    int serverfd = Open_clientfd(hostname, port);

    // send request to server as we read it from client (think echo)
    // a request consists of a GET line and zero or more headers
    // a blank line indicates the end of the request headers
    // remember: line endings/blank lines are "\r\n"
    char request[MAXLINE] = "";
    sprintf(request, "%s %s HTTP/1.0\r\n", method, path);
    // Add request headers
    while(Rio_readlineb(&client_rio, client_buf, MAXLINE) > 0) {
        if (!strcmp(client_buf, "\r\n")) {
            break;
        }
        strcat(request, client_buf);
    } 
    strcat(request, "\r\n");
    // printf("Request:\n%s", request);
    // fflush(stdout);

    // send request
    Rio_writen(serverfd, request, strlen(request));

    // initialize buffer for server and a cache item 
    char server_buf[MAXLINE] = "", new_cache_item[MAXCACHEITEM] = "";
    // initialize rio to server
    Rio_readinitb(&rio_server, serverfd);

    // read the response from the server
    // remember: response headers, then a blank line, then the response body
    size_t cache_size = 0;
    int n;
    while ((n = Rio_readnb(&rio_server, server_buf, MAXLINE)) > 0) { // keep reading until EOF
        cache_size += n; // keep track of cache item size
        strncat(new_cache_item, server_buf, n); // write to cache the the amount we just read
        Rio_writen(connfd, server_buf, n); // write to client the amount we just read
    }

    //printf("Cache saved(%zd)!!!\n---\n%s----\n", cache_size, new_cache_item);
    //fflush(stdout);

    // add cache
    cache_insert(uri, new_cache_item, cache_size);
    //cache_print();
    
    Close(serverfd); // close server connection
    Close(connfd); // close client connection
    return NULL;
}

int parse_uri(char *uri, char *hostname, char *port, char *path) {

    const char colon[2] = ":";
    const char slash[2] = "/";

    // create a copy of url so url won't be altered during parsing
    char uri_copy[MAXLINE];
    strcpy(uri_copy, uri+7);

    // extract PATH from uri
    char* path_start = index(uri_copy, '/');
    strcpy(path, path_start);
    
    // find PORT
    char* checkport = index(uri_copy, ':');
    char* token;
    if (checkport) { // if PORT is included in URI
        if (rindex(uri_copy, ':') != checkport) { //checks for only 1 occurance of ":", if more, illegal request
            return 1;
        }
        // extract host:port/...
        token = strtok(uri_copy, colon);
        strcpy(hostname, token);
        token = strtok(NULL, slash);
        strcpy(port, token);
    } else { // No port in URI
        //extract hostname and set port to default
        token = strtok(uri_copy, slash);
        strcpy(hostname, token);
        port = "80";
    }
    return 0;
} 


int main(int argc, char **argv) {
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    // initializing cache
    cache_init();

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);

        // creates a thread
        pthread_t tid;
        Pthread_create(&tid, NULL, handle_request, (void *) (long) connfd);
    }
    cache_free(); //freeing the whole cache
    Close(listenfd);
    exit(0);
}