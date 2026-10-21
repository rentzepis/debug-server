/* Proxy Lab
 * Jeff Brandon - jdbrando@andrew.cmu.edu
 * Cory Pruce - cpruce@andrew.cmu.edu
 *
 * We worked together to implement a proxy server with cacheing and concurrency
 * Jeff wrote the intial proxy while Cory focussed on implementing the cache
 * and concurrency support. After the basic proxy was finished Jeff helped debug
 * the cache. For more information on who did what we can make our github repository
 * public for your viewing pleasure.
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "cache.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16

/* Struct that represents a parsed request.
 * The buffer from which the domain and path were parsed is
 * also stored here so that in the event of a failed connection
 * the path can be reparsed using reparse() as though there is 
 * no domain specified.
 */
struct req_t {
    char* domain;
    char* path;
    char* hdrs;
    char* pathbuf;
};
typedef struct req_t req_t;
typedef struct sockaddr_in sockaddr_in;	//for my own sanity
typedef struct hostent hostent; 	//ditto

int process_request(int, req_t *);
int parse_req(char*, req_t*);
void reparse_req(req_t*);
char *handle_hdr(char*);
void forward_request(int, req_t);
void free_req(req_t);
void *thread(void* vargp);
void not_found(int);
void bad_request(int);

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

sbuf_t sbuf; 		//Shared buffer
sem_t w; 			//Mutual exclusion enforced on buffer
int num_entries;	//Count of cache entries
cache_obj * cache;	//Front of the Cache (implemented as a linked list)

/* After intiall error checks, bind and listen to the provided port
 * start some helper threads, then accept connections and place them in
 * the shared buffer for a helper thread to process.
 */
int main(int argc, char** argv)
{
    int i, listenport, listenfd, connfd;
    unsigned clientlen;
    sockaddr_in clientaddr;
    pthread_t tid;
		
    if(argc < 2){
        printf("usage: %s <port number to bind and listen>\n", argv[0]);
        exit(1);
    }
    
    Sem_init(&w, 0, 1);
    num_entries = 0;
    cache = NULL;
    
    listenport = atoi(argv[1]);
    sbuf_init(&sbuf, SBUFSIZE);
    listenfd = Open_listenfd(listenport);
    clientlen = sizeof(clientaddr);
  
    for(i = 0; i < NTHREADS; i++) /* prethreading, creating worker threads */
        Pthread_create(&tid, NULL, thread, &clientaddr);
 
    while(1){
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen); 
    	sbuf_insert(&sbuf, connfd);	// put in buffer    
    }

    return 0;
}

/* Consumes connection file descriptors from the shared buffer and 
 * processes them.
 */
void *thread(void *vargp){
    // avoid memory leak
    Pthread_detach(pthread_self());
    req_t request;
    int result;
    while(1){
        int connfd = sbuf_remove(&sbuf);
        if((result = process_request(connfd, &request)) == -1){
            fprintf(stderr,"process_request failed\n");
            bad_request(connfd);
            free_req(request);
            Close(connfd);
            continue;
        }

        forward_request(connfd, request);
        Close(connfd);	
    }	
}

/* Read a request from a connection socket and parses its information into a 
 * req_t struct.
 * Initally based on csapp echo() p.911
 * Allocates memory for request hdrs
 * returns -1 on failure, 1 if the content is local, and 0 if the content is
 * remote
 */
int process_request(int fd, req_t* req){
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    req->domain = NULL;
    req->path = NULL;
    req->hdrs = NULL;

    //Parse domain and path information    
    Rio_readinitb(&rio, fd);
    if((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){
        if(parse_req(buf, req) == -1)
            return -1;
    }
    else return -1;
    //parse header information
    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0){   
        if(strcmp(buf, "\r\n") == 0)
            break;
        if(req->hdrs != NULL){
            n = strlen(req->hdrs) + strlen(buf) + 1;
            req->hdrs = Realloc(req->hdrs,strlen(req->hdrs)+ n);
            strcat(req->hdrs, handle_hdr(buf));
        } else {
            req->hdrs = Malloc(n+1);
            strcpy(req->hdrs, handle_hdr(buf));
        }
    }
    return 0;
}

/* Use string library functions to extract domain and path from
 * an HTTP request.
 * Allocates memory for the request domain and path
 * returns 0 if client connection needs to be made, 1 if 
 * the content requested is local to the proxy
 */
int parse_req(char* buf, req_t* req){
    char* save, *p;

    req->pathbuf = Malloc(strlen(buf)+1);
    strcpy(req->pathbuf, buf);
    strtok_r(buf, " ", &save);			//GET
    strtok_r(NULL, "//", &save); 		//http:
    p = strtok_r(NULL, "/", &save);	 	//domain
    if(p) req->domain = Malloc(strlen(p)+1);
    else return -1;
    strcpy(req->domain, p);
    p = strtok_r(NULL, " ", &save);		//path
    if(strcmp(p, "HTTP/1.1\r\n") == 0 || strcmp(p, "favicon.ico") == 0){
        //no domain specified use localhost
        //update path to correct value 
        strtok_r(buf, "//", &save);
        p = strtok_r(NULL, " ", &save);
        if(p) req->path = Malloc(strlen(p)+1);
        else return -1;
        strcpy(req->path, p);
        req->domain = Realloc(req->domain, strlen("local")+1);
        strcpy(req->domain, "local");
        return 0;
    }
    if(p) req->path = Malloc(strlen(p)+1);
    else return -1;
    strcpy(req->path, p);
    return 0;
}

/* I the event that a connection fails for a request, use this function
 * to reparse the request assuming the domain was not specified. Therefore
 * we parse up to http:// and then the rest of the string until a <space> is 
 * reached as a path to a file on the machine the proxy is running 
 * on.
 */
void reparse(req_t* req){
    char* save, *p;
    strtok_r(req->pathbuf, "//", &save);
    p = strtok_r(NULL, " ", &save);
    req->path = Realloc(req->path, strlen(p) + 1);
    strcpy(req->path, p);
}

/* Parse a header, if it is a header that our proxy defines
 * statically replace the header with our predefined version.
 * Otherwise just add the the header to the existing header list
 * All memory dynamically allocated by this function is freed by this function.
 */
char *handle_hdr(char* buf){
    char* cp, * head;
    size_t size;
    size = strlen(buf) > strlen(user_agent_hdr) ? strlen(buf) \
                                                : strlen(user_agent_hdr);
    cp = (char*) Malloc(size+1);
    head = (char*) Malloc(51);
    strcpy(cp, buf);

    strcpy(head, strtok(buf, ":"));
    if(strcmp(head, "User-Agent") == 0)
        strcpy(cp, user_agent_hdr); 
    if(strcmp(head, "Accept") == 0) 
        strcpy(cp, accept_hdr);
    if(strcmp(head, "Accept-Encoding") == 0) 
        strcpy(cp, accept_encoding_hdr);
    if(strcmp(head, "Connection") == 0) 
        strcpy(cp, conn_hdr);
    if(strcmp(head, "Proxy-Connection") == 0) 
        strcpy(cp, prox_hdr);

    strcpy(buf, cp);
    Free(head);
    Free(cp);
    return buf;
}

/* Takes a request and forwards it to its destination server by opening a client
 * connection. Returns the response of the destination server.
 * This function frees memory allocated for the request also using free_req() 
 */
void forward_request(int fd, req_t request){
    int server;
    size_t n, total_read;
    cache_obj* entry;
    char *name, *portstr, http[1024], buf[MAXLINE], cachebuf[MAX_OBJECT_SIZE];
    rio_t rio;

    cachebuf[0] = '\0';
    name = strtok(request.domain, ":");
    portstr = strtok(NULL, ":");
    if(name == NULL){ 
        free_req(request);
        return;
    }
    if(portstr == NULL) portstr = "80";
    
    // checking the cache is still updating it (age)
    P(&w);
    if((entry = in_cache(request.path, num_entries, cache)) != NULL){
        V(&w);
        Rio_writen(fd, entry->buf, entry->obj_size);
    } else {
        V(&w);
         server = Open_clientfd_r(name, atoi(portstr));
         if(server != -1){
            sprintf(http, "GET /%s HTTP/1.0\r\n", request.path);
            strcat(http, request.hdrs);
            Rio_writen(server, http, strlen(http));
            Rio_writen(server, "\r\n", 2);
        } else {
            reparse(&request);
            char *wdpath;
            wdpath = getcwd(NULL,0);
            wdpath = Realloc(wdpath, strlen(wdpath) + strlen(request.path) +1);
            strcat(wdpath, request.path);
            server = open(wdpath, O_RDONLY);
            Free(wdpath);
            if(server == -1){
                not_found(fd);
                free_req(request);
                return;
            }
        }
    	Rio_readinitb(&rio, server);

    	total_read = 0;
    	while((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0){
            if(total_read+n <= MAX_OBJECT_SIZE){
                strcat(cachebuf, buf);
            }
            total_read += n;
            Rio_writen(fd, buf, n);
    	}
        // cache update, critical section
        if(total_read <= MAX_OBJECT_SIZE){
            P(&w);
            cache = cache_write(request.path, cachebuf, num_entries, cache);
            num_entries++;
            V(&w);
        } 
    }
    free_req(request);
}

/* Helper function to free allocations made for a request
 */
void free_req(req_t request){
    Free(request.domain);
    Free(request.path);
    Free(request.hdrs);
    Free(request.pathbuf);
}

/* Helper function that writes a simple html error page to a file descriptor
 * for 404 error: not found
 */
void not_found(int fd){
    Rio_writen(fd,"<html>\r\n", 8);
    Rio_writen(fd,"<body>\r\n", 8);
    Rio_writen(fd,"404: not found", 14);
    Rio_writen(fd,"</body>\r\n", 9);
    Rio_writen(fd,"</html>\r\n", 9);
}

/* Helper function that writes a simple html error page to a file descriptor
 * for 400 error: bad request 
 */
void bad_request(int fd){
    Rio_writen(fd,"<html>\r\n", 8);
    Rio_writen(fd,"<body>\r\n", 8);
    Rio_writen(fd,"400: bad request", 16);
    Rio_writen(fd,"</body>\r\n", 9);
    Rio_writen(fd,"</html>\r\n", 9);
}
