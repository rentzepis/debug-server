#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* FIXME: Debugging tools switch */
#define DEBUG 0

typedef struct reqline {
    char method[10];
    char uri[300];
    char version[100];
    char hostname[200];
    char path[1000];
} reqline;

typedef struct reqheader {
    char name[MAXLINE];
    char data[MAXLINE];
    struct reqheader *next;        // next link to implement a linked list
} reqheader;

typedef struct cacheblock {
    char uri[300];
    size_t size;
    char* data;
    struct cacheblock *next;        // next link to implement a linked list
    clock_t access;                 // access time for LRU implementation
} cacheblock;

/* Root and tail node of linked list of cache blocks */
static cacheblock *cache_root = NULL;
static cacheblock *cache_tail = NULL;

/* Current total cache size */
static size_t cache_size = 0;

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void *proxy_thread(void *connfd_pt);

void read_request(int connfd, reqline *line, reqheader **rootpt, jmp_buf env);
void parse_req_line(reqline *line, char *buf, int connfd, jmp_buf env);
void parse_req_header(char *buf, int connfd, reqheader **rootpt, jmp_buf env);
void prepare_headers(reqline *line, reqheader **rootpt);

int send_request(reqline *line, reqheader *root, jmp_buf env);
void receive_response(int connfd, int requestfd, char *uri, jmp_buf env);
void error_response(int fd, char *status, char *msg, char *longmsg, jmp_buf env);

void insert_header(reqheader **rootpt, reqheader *header);
reqheader *find_header(reqheader *root, char *name);
void free_headers(reqheader *root);

void init_cache();
cacheblock *insert_cache(char *uri, char *data, size_t size);
void evict_cache();
cacheblock *find_cache(char *uri);

void Rwriten(int fd, char *buf, size_t len, jmp_buf env);
ssize_t Rreadlineb(rio_t *rp, char *buf, size_t len, jmp_buf env);

void print_headers(reqheader *root);


int main(int argc, char **argv) {
    char *port;         // listening port
    int listenfd;
    int connfd;
    int *connfd_pt;
    pthread_t tid;

    struct sockaddr_in clientaddr;
    unsigned int clientlen;

    /* Check cmdline arguments to get port number */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = argv[1];

    /* Set to ignore SIGPIPE */
    Signal(SIGPIPE, SIG_IGN);

    /* Initialize the global cache list */
    init_cache();

    /* Get listenfd */
    listenfd = Open_listenfd(port);

    /* Main loop */
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (DEBUG) {
            printf("Connection accepted - ");
            printf("connfd %d, port %d\n", connfd, clientaddr.sin_port);
        }

        /* Create a new thread */
        connfd_pt = (int *)Malloc(sizeof(int));
        *connfd_pt = connfd;            // copy to heap memory
        Pthread_create(&tid, NULL, proxy_thread, connfd_pt);
    }
}


void *proxy_thread(void *connfd_pt) {
    int connfd = *(int *)connfd_pt;
    int requestfd;
    reqline *line;
    reqheader *root = NULL;             // root of linked list of headers
    jmp_buf env;                        // buffer for longjmp

    Free(connfd_pt);                    // free the argument
    Pthread_detach(pthread_self());     // detach current thread

    /* An error has been thrown; Close current connection */
    if (setjmp(env) == 1) {
        Close(connfd);
        Free(line);
        free_headers(root);

        return NULL;
    }

    /* Read request from client */
    line = (reqline *)Malloc(sizeof(reqline));
    read_request(connfd, line, &root, env);

    /* Send request to target server */
    requestfd = send_request(line, root, env);

    /* Receive request from target server */
    receive_response(connfd, requestfd, line->uri, env);

    /* Finish current thread */
    Close(connfd);
    Free(line);
    free_headers(root);

    return NULL;
}


/*
 * [read_request] Reads every line in request, and then parses
 *      reqeust line and request header separately.
 */
void read_request(int connfd, reqline *line, reqheader **rootpt, jmp_buf env) {
    char buf[MAXLINE];
    rio_t rio;
    rio_t *rp = &rio;

    Rio_readinitb(rp, connfd);

    /* Read request line */
    Rreadlineb(rp, buf, MAXLINE, env);
    parse_req_line(line, buf, connfd, env);
    if (DEBUG) {
        printf("reqline - %s %s %s\n", line->method, line->uri, line->version);
        printf("\thostname %s path %s\n", line->hostname, line->path);
    }

    /* Read request headers */
    Rreadlineb(rp, buf, MAXLINE, env);
    while (strcmp(buf, "\r\n")) {
        parse_req_header(buf, connfd, rootpt, env);

        if (DEBUG) {
            printf("req - %s\n", buf);
            print_headers(*rootpt);
        }

        Rreadlineb(rp, buf, MAXLINE, env);    // read next line
    }

    /* Add missing headers */
    prepare_headers(line, rootpt);

    if (DEBUG) print_headers(*rootpt);
}


/*
 * [parse_req_line] Parses a request line. Used inside `read_request`.
 */
void parse_req_line(reqline *line, char *buf, int connfd, jmp_buf env) {
    char *host_pt, *path_pt;        // pointers to starting points of each item

    /* Scan request line */
    sscanf(buf, "%s %s %s", line->method, line->uri, line->version);

    /* Check method */
    if (strcasecmp(line->method, "GET")) {
        error_response(connfd, "501", "Not Implemented", "Unimplemented method.", env);
    }

    /* Retrieve host name and path */
    host_pt = strstr(line->uri, "http://") + 7;
    path_pt = strchr(host_pt, '/');
    if (path_pt) {
        /* URI contains path */
        strncpy(line->hostname, host_pt, path_pt - host_pt);
        line->hostname[path_pt - host_pt] = '\0';
        strcpy(line->path, path_pt);
    } else {
        /* No path; Add home directory as default path */
        strcpy(line->hostname, host_pt);
        strcpy(line->path, "/");
    }
}


/*
 * [parse_req_header] Parses a line of request header.
 *      Used inside `read_request`.
 */
void parse_req_header(char *buf, int connfd, reqheader **rootpt, jmp_buf env) {
    reqheader *header;
    char *delim;

    /* Check header format */
    if (!(delim = strstr(buf, ": "))) {
        error_response(connfd, "400", "Bad Request", "Request header in a wrong format.", env);
    }

    /* Retrieve header name and data */
    header = (reqheader *)Malloc(sizeof(reqheader));
    *delim = '\0';
    strcpy(header->name, buf);
    strcpy(header->data, delim+2);
    *delim = ':';

    insert_header(rootpt, header);      // append as a new node
}


/*
 * [prepare_headers] Adds some new headers to the global list
 *      to make sure that it contains all necessary headers.
 */
void prepare_headers(reqline *line, reqheader **rootpt) {
    reqheader *header;

    /* Host */
    if (!find_header(*rootpt, "Host")) {
        header = (reqheader *)Malloc(sizeof(reqheader));
        strcpy(header->name, "Host");
        strcpy(header->data, line->hostname);
        strcat(header->data, "\r\n");
        insert_header(rootpt, header);
    }

    /* User-Agent */
    if (!find_header(*rootpt, "User-Agent")) {
        header = (reqheader *)Malloc(sizeof(reqheader));
        strcpy(header->name, "User-Agent");
        strcpy(header->data, user_agent_hdr);
        insert_header(rootpt, header);
    }

    /* Connection */
    if (!find_header(*rootpt, "Connection")) {
        header = (reqheader *)Malloc(sizeof(reqheader));
        strcpy(header->name, "Connection");
        strcpy(header->data, "close\r\n");
        insert_header(rootpt, header);
    }

    /* Proxy-Connection */
    if (!find_header(*rootpt, "Proxy-Connection")) {
        header = (reqheader *)Malloc(sizeof(reqheader));
        strcpy(header->name, "Proxy-Connection");
        strcpy(header->data, "close\r\n");
        insert_header(rootpt, header);
    }
}


/*
 * [send_request] Makes a complete request line and request headers
 *      and sends them to the target server. Generally returns fd of
 *      the socket which is connected to the target server. Returns
 *      -1 if the desired content is already cached and additional
 *      connection to server is not needed.
 */
int send_request(reqline *line, reqheader *root, jmp_buf env) {
    char req_domain[200];
    reqheader *header;
    char *port;
    char request[MAXLINE];
    int requestfd;

    /* Check if the desired content is already cached */
    if (find_cache(line->uri)) {
        return -1;
    }

    /* Retrieve request domain */
    if (strlen(line->hostname)) {
        strcpy(req_domain, line->hostname);
    } else if ((header = find_header(root, "Host"))) {
        strcpy(req_domain, header->data);
    }

    /* Retrieve request port */
    port = strchr(req_domain, ':');
    if (port) {
        *port = '\0';               // remove port number in domain
        port++;
    } else {
        /* No port nubmer; Use default port 80 */
        port = "80";
    }

    /* Write request line */
    sprintf(request, "%s %s HTTP/1.0\r\n", line->method, line->path);

    /* Write request headers */
    header = root;
    while (header) {
        sprintf(request, "%s%s: %s\r\n", request, header->name, header->data);
        header = header->next;
    }
    strcat(request, "\r\n");        // end of request

    /* Open request file descriptor */
    requestfd = Open_clientfd(req_domain, port);

    /* Send request */
    Rwriten(requestfd, request, strlen(request), env);

    return requestfd;
}


/*
 * [receive_response] Receives response from the target server
 *      and convey the response to the client.
 */
void receive_response(int connfd, int requestfd, char *uri, jmp_buf env) {
    rio_t req_rio;
    char buf[MAXLINE];
    ssize_t nbytes;
    cacheblock *block;
    char content[MAX_OBJECT_SIZE] = "";
    int is_too_large = 0;

    /* Data already cached */
    if (requestfd == -1) {
        if (DEBUG) printf("Using cached data\n");

        block = find_cache(uri);
        Rwriten(connfd, block->data, block->size, env);
        return;
    }

    /* Read lines in response */
    Rio_readinitb(&req_rio, requestfd);
    
    while ((nbytes = Rreadlineb(&req_rio, buf, MAXLINE, env)) > 0) {
        /* Convey response to client */
        Rwriten(connfd, buf, (size_t)nbytes, env);

        /* Save response for caching */
        if (!is_too_large) {
            if (strlen(content) + nbytes < MAX_OBJECT_SIZE) {
                strncat(content, buf, nbytes);
            } else {
                is_too_large = 1;
            }
        }
    }

    Close(requestfd);
    if (DEBUG) printf("Response ended\n");

    /* Insert response data into the cache */
    if (!is_too_large && strlen(content) < MAX_CACHE_SIZE) {
        while (strlen(content) + cache_size >= MAX_CACHE_SIZE) {
            evict_cache();          // reserve enough space
        }
        insert_cache(uri, content, strlen(content)+1);
    }
    
    else if (DEBUG) printf("Content too large to be cached\n");
    if (DEBUG) printf("Cache status: %ld (%ld +) / %ld\n", cache_size, strlen(content), (size_t)MAX_CACHE_SIZE);
}


/*
 * [error_response] Sends a HTTP response containing information
 *      about a raised error.
 */
void error_response(int fd, char *status, char *msg, char *longmsg, jmp_buf env) {
    char buf[MAXLINE], body[MAXLINE];

    /* Write response body */
    sprintf(body, "<html><title>Proxy error </title>\r\n");
    sprintf(body, "%s%s: %s\r\n", body, status, msg);
    sprintf(body, "%s<p>%s\r\n", body, longmsg);

    /* Send HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", status, msg);
    sprintf(buf, "%sContent-type: text/html\r\n", buf);
    sprintf(buf, "%sContent-length: %ld\r\n\r\n", buf, strlen(body));
    Rwriten(fd, buf, strlen(buf), env);
    Rwriten(fd, body, strlen(body), env);

    /* Get back to the thread routine and close the current connection */
    longjmp(env, 1);
}


/* 
 * [insert_header] Appends a new request header object to
 *      the global linked list.
 */
void insert_header(reqheader **rootpt, reqheader *header) {
    reqheader *pt;

    if (*rootpt == NULL) {
        /* List is empty */
        *rootpt = header;
        header->next = NULL;
    }

    else {
        /* List is not empty */
        pt = *rootpt;
        while (pt->next != NULL) {
            pt = pt->next;
        }
        pt->next = header;
        header->next = NULL;
    }
}


/*
 * [find_header] Searches the global header list and find a
 *      header by its name.
 */
reqheader *find_header(reqheader *root, char *name) {
    reqheader *header = root;

    while (header) {
        if (strcmp(header->name, name) == 0) {
            return header;          // header name matches
        }
        header = header->next;      // move to the next node
    }

    return NULL;                    // not found
}


/*
 * [free_header] Frees the entire global header list.
 */
void free_headers(reqheader *root) {
    reqheader *pt = root;
    reqheader *next;

    /* Free every node */
    while (pt) {
        next = pt->next;
        Free(pt);
        pt = next;
    }
}


/*
 * [init_cache] Initializes the root of cache list as a dummy node.
 */
void init_cache() {
    cache_root = (cacheblock *)Malloc(sizeof(cacheblock));
    strcpy(cache_root->uri, "");
    cache_root->size = 0;
    cache_root->data = NULL;
    cache_root->next = NULL;
    cache_root->access = clock();

    cache_tail = cache_root;
}


/*
 * [insert_cache] Allocates a new cache block and inserts it
 *      to the linked list.
 */
cacheblock *insert_cache(char *uri, char *data, size_t size) {
    cacheblock *new = (cacheblock *)Malloc(sizeof(cacheblock));
    cache_tail->next = new;         // insert as the last node
    cache_tail = new;               // update list tail

    strcpy(new->uri, uri);
    new->data = (char *)Malloc(size * sizeof(char));
    strncpy(new->data, data, size);
    new->size = size;
    new->next = NULL;
    new->access = clock();
    cache_size += size;             // update total cache size

    return new;
}


/*
 * [evict_cache] Finds a victim block by LRU policy and remove it
 *      from the linked list to get more space.
 */
void evict_cache() {
    cacheblock *block;
    cacheblock *victim;             // victim block to evict
    cacheblock *prev;               // block right before victim
    clock_t min = clock();

    if (DEBUG) printf("### evict_cache (current %ld)\n", cache_size);

    /* Find victim with the smallest access time value (by LRU policy) */
    block = cache_root;
    while (block->next) {
        if (block->next->access < min) {
            victim = block->next;
            prev = block;
            min = block->next->access;
        }
        block = block->next;
    }

    /* Remove the victim from the linked list */
    prev->next = victim->next;
    cache_size -= victim->size;     // update total cache size
    Free(victim->data);
    Free(victim);

    if (DEBUG) printf("\tafter eviction %ld\n", cache_size);
}


/* 
 * [find_cache] Finds a cache block by uri (hostname and path).
 *      Returns NULL if not found.
 */
cacheblock *find_cache(char *uri) {
    cacheblock *block = cache_root->next;

    while (block) {
        if (strcmp(block->uri, uri) == 0) {
            block->access = clock();        // update access time
            return block;
        }
        block = block->next;
    }
    return NULL;                            // not found
}


/*
 * [Rwiten] A wrapper of rio_writen(). When EPIPE error is thrown
 *      while writing a socket, uses longjmp() to get back
 *      to the main thread routine.
 */
void Rwriten(int fd, char *buf, size_t len, jmp_buf env) {
    ssize_t nbytes = rio_writen(fd, buf, len);
    
    if (nbytes < 0 && errno == EPIPE) {
        /* Jump to the thread routine when connection is closed */
        longjmp(env, 1);
    } else if (nbytes != len) {
        unix_error("Rio_writen error");
    }
}


/*
 * [Rreadlineb] A wrapper of rio_readlineb(). When ECONNRESET
 *      error is thrown while reading a socket, uses longjmp()
 *      to get back to the main thread routine.
 */
ssize_t Rreadlineb(rio_t *rp, char *buf, size_t len, jmp_buf env) {
    ssize_t nbytes = rio_readlineb(rp, buf, len);

    if (nbytes < 0 && errno == ECONNRESET) {
        if (errno == ECONNRESET) {
            /* Jump to the thread routine when connection is closed */
            longjmp(env, 1);
        } else {
            unix_error("Rio_readlineb error");
        }
    }

    return nbytes;
}


/*** Debugging tools ***/

void print_headers(reqheader *root) {
    reqheader *pt = root;

    printf("*** print headers ***\n");
    while (pt) {
        printf("%s: %s", pt->name, pt->data);
        pt = pt->next;
    }
    printf("\n");
}