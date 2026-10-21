#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

#define CACHE_LINE_NUM 10

struct cache_line {
    char key[MAXLINE];
    char used;
    size_t cache_size;
    char * cache_location;
    struct cache_line* next;
};

int readcnt;
sem_t mutex, w;

static struct cache_line* cache_line_root;

void init_cache();
size_t find_cache(char * key, char * content);
void insert_cache(char * key, char * content, size_t content_size);

void* thread(void *vargp);
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *hostname, char *port, char *request);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    init_cache();

    int listenfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        int connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
            Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                        port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, (void*)(long)connfd);                                      //line:netp:tiny:doit
    }
}

void init_cache()
{
    void * cache_location = Malloc(MAX_OBJECT_SIZE * 10);
    struct cache_line* last = NULL;
    for(int i = CACHE_LINE_NUM - 1; i >=0; --i) {
        struct cache_line *p = Malloc(sizeof(struct cache_line));
        p->cache_location = cache_location + i * MAX_OBJECT_SIZE;
        p->used = 0;
        p->cache_size = 0;
        p->next = last;
        last = p;
    }
    cache_line_root = last;

    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
    readcnt = 0;
}

size_t find_cache(char * key, char * content)
{
    P(&mutex);
    readcnt ++;
    if(readcnt == 1) P(&w);
    V(&mutex);

    struct cache_line *p;
    size_t size = 0;
    // read happens
    for(p = cache_line_root; p && p->used; p=p->next) {
        if(strcmp(key, p->key) == 0) {
            size = p->cache_size;
            memcpy(content, p->cache_location, size);
            break; 
        }
    }

    P(&mutex);
    readcnt--;
    if(readcnt == 0) V(&w);
    V(&mutex);

    if(size > 0 && p!= cache_line_root) {
        P(&w);
        // writer happens
        for(struct cache_line* t = cache_line_root; t && t->used; t=t->next) {
            // LRU
            if(t->next == p) {
                t->next = p->next;
                p->next = cache_line_root;
                cache_line_root = p;
            }
        }
        V(&w);
    }
    return size;
}

void insert_cache(char * key, char * content, size_t content_size)
{
    P(&w);
    struct cache_line *p;
    for(p = cache_line_root; p; p=p->next) {
        if(p->used == 0 || p->next == NULL) {
            p->used = 1;
            strcpy(p->key, key);
            p->cache_size = content_size;
            memcpy(p->cache_location, content, content_size);
            break;
        }
    }

    if(p != cache_line_root) {
        for(struct cache_line*t = cache_line_root; t; t=t->next) {
            if(t->next == p) {
                t->next = p->next;
                p->next = cache_line_root;
                cache_line_root = p;
            }
        }
    }
    V(&w);
}

void* thread(void *vargp)
{
    int connfd = (int)(long)vargp;
    Pthread_detach(pthread_self());
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int fd)
{
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], request[MAXLINE];
    rio_t rio;

    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
        return;
    printf("%s\n", buf);
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }                                                    //line:netp:doit:endrequesterr
    read_requesthdrs(&rio);                              //line:netp:doit:readrequesthdrs

    if(!parse_uri(uri, hostname, port, request)) {
        return;
    }
    printf("uri:%s\n", uri);
    printf("hostname:%s\n", hostname);
    printf("port:%s\n", port);
    printf("request:%s\n", request);

    char * content = Malloc(MAX_OBJECT_SIZE);
    size_t size = find_cache(uri, content);
    if(size > 0) {
        printf("Got Cache for : %s, Lenght = %ld\n", uri, size);
        Rio_writen(fd, content, size);
        return;
    }

    int clientfd = Open_clientfd(hostname, port);
    rio_t wrio;
    Rio_readinitb(&wrio, clientfd);
    char wbuf[MAXLINE];
    strcpy(wbuf, "GET ");
    strcat(wbuf, request);
    strcat(wbuf, " HTTP/1.0\r\n");
    Rio_writen(clientfd, (void*)wbuf, strlen(wbuf));
    strcpy(wbuf, "Host: ");
    strcat(wbuf, hostname);
    strcat(wbuf, "\r\n");
    Rio_writen(clientfd, (void*)wbuf, strlen(wbuf));
    Rio_writen(clientfd, (void*)user_agent_hdr, strlen(user_agent_hdr));
    Rio_writen(clientfd, (void*)connection_hdr, strlen(connection_hdr));
    Rio_writen(clientfd, (void*)proxy_connection_hdr, strlen(proxy_connection_hdr));
    Rio_writen(clientfd, "\r\n", 2);
    
    size_t size_wrote = 0;
    while(1) {
        ssize_t s = Rio_readnb(&wrio, wbuf, MAXLINE);
        if(s > 0) {
            Rio_writen(fd, wbuf, s);
        } else {
            break;
        }
        if(size_wrote + s <= MAX_OBJECT_SIZE) {
            memcpy(content + size_wrote, wbuf, s);
            size_wrote += s;
        } else {
            size_wrote = MAX_OBJECT_SIZE +1;
        }
    }
    
    if(size_wrote <= MAX_OBJECT_SIZE) {
        insert_cache(uri, content, size_wrote);
        printf("Write Cache for %s, Length = %ld\n", uri, size_wrote);
    }

    Free(content);
    Close(clientfd);
}

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

int parse_uri(char *uri, char *hostname, char *port, char *request)
{
    uri = strstr(uri, "//");
    if(!uri)  return 0;
    uri += 2;
    char * colon = strstr(uri, ":");
    char * splash = strstr(uri, "/");
    if(!splash) return 0;
    if(colon) {
        strncpy(hostname, uri, (size_t)(colon - uri));
        strncpy(port, colon+1, splash - colon - 1);
    } else {
        strncpy(hostname, uri, (size_t)(splash-uri));
        strcpy(port, "80");
    }
    strcpy(request, splash);
    return 1;
}


void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}