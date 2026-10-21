#include "csapp.h"

#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wformat-overflow="
#pragma GCC diagnostic ignored "-Wrestrict"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_WEB 15

#define MAX_CLIENT 16
#define NTHREAD 4

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/71.0.3578.98 Safari/537.36\r\n";
/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the 
 *     GET method to serve static and dynamic content.
 */

typedef struct {
    int *buf;
    int n;
    int front;
    int rear;
    
    sem_t mutex;
    sem_t slots;
    sem_t items;
} sbuf_t;

typedef struct {
    char cache[MAX_OBJECT_SIZE];
    char dst[MAXLINE];
    int size;
} web_obj;

typedef struct {
    web_obj *obj;
    int size_all;
    int n;
    int objn_cnt;
    int read_cnt;
    int front, rear;
    
    sem_t mutex;
    sem_t write;
} web_cache;

void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);

void cache_init(web_cache *cache, int n);
char* cache_find(web_cache *cache, char* dst);
void cache_put(web_cache *wcache, char* dst, char* cache);
void cache_remove(web_cache *cache, int pos);

void sigpipe_handler(int sig);
void forward(int clientfd);
void backward(rio_t *rp, rio_t *rp_server, web_cache *cache, char *dst);
void read_requesthdrs(rio_t *rp);
void write_requesthdrs(int fd, char *method, char *filename, char *hostname, char *port);
int parse_uri(char *uri, char *hostname, char *port, char *filename);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

void *doit(void *vargp);

static sbuf_t sbuf;
static web_cache wbuf;

int main(int argc, char **argv) 
{
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, sigpipe_handler);
    
    pthread_t tid;
    sbuf_init(&sbuf, MAX_CLIENT);
    cache_init(&wbuf, MAX_WEB);
    
    for (int i = 0; i < NTHREAD; i++)
        Pthread_create(&tid, NULL, doit, NULL);
    
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); 
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        //printf("Accepted connection from (%s, %s)\n", hostname, port);
        sbuf_insert(&sbuf, connfd);
    }
}
/* $end tinymain */

static char* sigpipe_msg = "SIGPIPE Catch!\n";
void sigpipe_handler(int sig)
{
    write(STDOUT_FILENO, sigpipe_msg, strlen(sigpipe_msg));
}

/*
 * forward - handle one HTTP request/response transaction
 */
/* $begin forward */
void forward(int clientfd) 
{
    int is_static;
    struct stat sbuf;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char filename[MAXLINE], cgiargs[MAXLINE], hostname[MAXLINE], port[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, clientfd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(clientfd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs

    sscanf(buf, "%s %s %s", method, uri, version);
    char *ptr;
    if ((ptr = cache_find(&wbuf, uri)) != NULL) {
        Rio_writen(clientfd, ptr, strlen(ptr));
        Free(ptr);
        return;
    }
    ptr = Malloc(strlen(uri));
    strcpy(ptr, uri);
    if (parse_uri(uri, hostname, port, filename) == -1)
        return;
    //printf("uri=:%s\nhostname=%s\nport=%s\nfilename=%s\ncgiargs=%s\n", uri, hostname, port, filename, cgiargs);
    
    int serverfd = Open_clientfd(hostname, port);
    write_requesthdrs(serverfd, method, filename, hostname, port);
    
    Rio_readnb(&rio, buf, rio.rio_cnt);
    rio_t rio_server;
    Rio_readinitb(&rio_server, serverfd);
    backward(&rio, &rio_server, &wbuf, ptr);
    Free(ptr);
    Close(serverfd);
    return;
}
/* $end forward */

/*
 * backward - transfer message from client to the end server.
 */
/* $begin backward */
void backward(rio_t *rp, rio_t *rp_server, web_cache *cache, char *dst)
{
    char buf[MAXLINE];
    char obj[MAX_OBJECT_SIZE];
    int n, size = 0;
    while((n = Rio_readlineb(rp_server, buf, MAXLINE)) != 0) {
        strcpy(obj+size,buf);
        size += n;
        Rio_writen(rp->rio_fd, buf, n);
    }
    cache_put(cache, dst, obj);
}
/* $end backward */

/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    return;
}

/* $end read_requesthdrs */

/*
 * write_requesthdrs - write HTTP request headers to the server
 */
/* $begin write_requesthdrs */
void write_requesthdrs(int fd, char *method, char *filename, char *hostname, char *port) 
{
    char buf[MAXLINE];

    sprintf(buf, "%s %s HTTP/1.0\r\n", method, filename);
    if (port[0] != '\0')
        sprintf(buf, "%sHOST: %s:%s\r\n", buf, hostname, port);
    else 
        sprintf(buf, "%sHOST: %s\r\n", buf, hostname);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-Connection: close\r\n", buf);
    sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
    Rio_writen(fd, buf, strlen(buf));
    return;
}

/* $end write_requesthdrs */

/*
 * parse_uri - parse URI into filename and CGI args
 *             return 0 if dynamic content, 1 if static
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *hostname, char *port, char *filename)
{
    if (strstr(uri, "http://"))
        uri = uri + 7;
    
    char *ptr;
    
    ptr = index(uri, '/');
    if (ptr) {
        strcpy(filename, ptr);
        *ptr = '\0';
    } else {
        printf("Could not find Symbol /\n");
        return -1;
    }
    ptr = index(uri, ':');
    if (ptr) {
        strcpy(port, ptr+1);
        *ptr='\0';
    }
    strcpy(hostname, uri);
    return 0;
}
/* $end parse_uri */

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */

void *doit(void *vargp)
{
    Pthread_detach(pthread_self());
    while (1) {
        int connfd = sbuf_remove(&sbuf);
        forward(connfd);
        Close(connfd);
    }
}

void sbuf_init(sbuf_t *sp, int n)
{
    sp->buf = Calloc(n, sizeof(int));
    sp->n = n;
    sp->front = sp->rear = 0;
    Sem_init(&sp->mutex, 0, 1);
    Sem_init(&sp->slots, 0, n);
    Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp)
{
    Free(sp->buf);
}

void sbuf_insert(sbuf_t *sp, int item)
{
    P(&sp->slots);
    P(&sp->mutex);
    sp->buf[(++sp->rear)%(sp->n)] = item;
    V(&sp->mutex);
    V(&sp->items);
}

int sbuf_remove(sbuf_t *sp)
{
    int item;
    P(&sp->items);
    P(&sp->mutex);
    item = sp->buf[(++sp->front)%(sp->n)];
    V(&sp->mutex);
    V(&sp->slots);
    return item;
}

void cache_init(web_cache *cache, int n)
{
    cache->n = n;
    cache->size_all = cache->read_cnt = cache->rear = cache->front = cache->objn_cnt = 0;
    cache->obj = Calloc(n, sizeof(web_obj));
    Sem_init(&cache->mutex, 0, 1);
    Sem_init(&cache->write, 0, 1);
}

char* cache_find(web_cache *cache, char* dst)
{
    P(&cache->mutex);
    cache->read_cnt++;
    if (cache->read_cnt == 1)
        P(&cache->write);
    V(&cache->mutex);
    
    int i, n = cache->n;
    int l = (cache->front+1) % n, len = cache->objn_cnt;
    char *ans;
    for (i = 0; i < len; i++)
    {
        web_obj *obj = cache->obj + l;
        if (!strcmp(obj->dst, dst)) {
            int size = obj->size;
            ans = Malloc(size);
            strcpy(ans, obj->cache);
            break;
        }
        l = (l + 1) % n;
    }
    
    P(&cache->mutex);
    cache->read_cnt--;
    if (cache->read_cnt == 0)
        V(&cache->write);
    V(&cache->mutex);
    if (i == len) return NULL;
    else return ans;
}

void cache_put(web_cache *wcache, char* dst, char* cache)
{
    P(&wcache->write);
    
    int cnt = wcache->objn_cnt, n = wcache->n;
    if (cnt == n) {
        int pos = (wcache->front+1) % n;
        cache_remove(wcache, pos);
    }
    
    int pos = (++wcache->rear) % n, size = strlen(cache);
    web_obj *obj = wcache->obj + pos;
    strcpy(obj->dst, dst);
    strcpy(obj->cache, cache);
    obj->size = size;
    
    wcache->size_all += size;
    wcache->objn_cnt++;
    while(wcache->size_all > MAX_CACHE_SIZE) {
        int pos = (wcache->front+1) % n;
        cache_remove(wcache, pos);
    }
    
    V(&wcache->write);
}

void cache_remove(web_cache *cache, int pos)
{
    web_obj *obj = cache->obj + pos;
    cache->size_all -= obj->size;
    cache->front = (cache->front+1) % cache->n;
    cache->objn_cnt--;
}
