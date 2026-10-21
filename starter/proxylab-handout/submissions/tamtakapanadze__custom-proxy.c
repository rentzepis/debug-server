#include <stdio.h>
#include "csapp.h"
#include <stdbool.h>
#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h> 

/*
15-213 Introduction to Computer Systems
A simple HTTP proxy that caches web objects. 
tkapanad - ProxyLab

<< Part 1>>
For the first part of the lab, I setted up the proxy to accept incoming connections, 
read and parse requests, forward requests to web servers, read the servers’ responses, 
and forward those responses to the corresponding clients. This first part 
involves basic HTTP operation and how to use sockets to write programs that communicate
over network connections. 

<< Part 2 >>
In the second part, I upgraded the proxy to deal with multiple concurrent
connections. I'm dealing with concurrency. 

<< Part 3 >>
In the third part, I added caching to my proxy using a simple main memory 
cache of recently accessed web content.
The cache is implemebted as a queue of structs.

***Notes: Queue function are from CacheLab, Some code is from tiny.c and lecture slides.
*/


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_LINES MAX_CACHE_SIZE/MAX_OBJECT_SIZE

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int parse_uri(char *uri, char *host, char *portnum, char *path);
void read_requesthdrs(rio_t *rp, char* headersbuf);
void doit(int fd);
void send_requesthdrs(int fd, char* headersbuf, char* host);
void clienterror(int fd, char *cause, char *errnum, 
     char *shortmsg, char *longmsg);
void receivefH(rio_t *Hrio, int fd, char* uri);
void ignore (int sig);
void *thread(void *vargp);

typedef struct cache_entry
{
  char uri[MAXLINE];
  char* addr;
  int size;
}entry_t;

/**********************/
/*** Implementation ***/
/**********************/
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< QUEUES

typedef struct list_node list;
struct list_node {
  void *data;
  list *next;
};

typedef struct queue_header queue;
struct queue_header {
  list *front;
  list *back;
  size_t size;
};

typedef struct queue_header* queue_t;


/* O(1) */
queue_t queue_new()
//@ensures \result != NULL; 
{
  queue_t new = calloc(1, sizeof(queue));
  new->front = NULL;
  new->size = 0;
  return new;
}

/* O(1) */
size_t queue_size(queue_t Q)
//@requires Q != NULL;
//@ensures \result >= 0; 
{
  return Q->size;
}

/* O(1) -- adds an item to the back of the queue */
void enq(queue_t Q, void* x)
//@requires Q != NULL; 
{
  list* node = calloc(1, sizeof(list));
  if(Q->front == NULL)
  {
    Q->back = node;
    Q->front = node;
    Q->back->data = x;
    Q->size = Q->size + 1;
    Q->back->next = NULL;
  }
  else
  {
    Q->back->next = node;
    Q->back = node;
    Q->back->data = x;
    Q->size = Q->size + 1;
    Q->back->next = NULL;
  }
}




/* O(1) -- removes an item from the front of the queue */
void* deq(queue_t Q)
//@requires Q != NULL && queue_size(Q) > 0; 
{
  list* to_remove = Q->front;
  Q->front = Q->front->next;
  Q->size = Q->size - 1;
  void* lm = to_remove->data;
  free(to_remove);
  return lm;
  
}


/* O(i) -- doesn't remove the item from the queue */
void *queue_peek(queue_t Q, size_t i)
//@requires Q != NULL && 0 <= i && i < queue_size(Q); 
{
  list* peeked = Q->front;
  size_t j;
  for(j = 0; j < i; j++)
  { 
    peeked = peeked->next;
  }
  return peeked->data;
}


// searches and removes the element from the queue
void* queue_remove(queue_t Q, char* urI)
{
  size_t n = queue_size(Q);
  queue_t new = queue_new();
  size_t i, j, k;
  void* item = NULL;

  for (j = 0; j < n; j++)
  {
    void* m = deq(Q);
    if (strcmp(((entry_t*)m)->uri, urI))
    { 
      enq(new, m);
    } 
    else item = m;
  }

  if (item) k = n - 1;
  else k = n;

  for(i = 0; i < k; i++) 
  {
    void* l =  deq(new);
    enq(Q, l);
  }
  free(new); 
  return item; 
}

// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< QUEUES


entry_t* lookup (queue_t Q, char* uri);
static queue_t cache; 

int readcnt;    /* Initially = 0 */ 
sem_t mutex, w; /* Initially = 1 */


/*
 * lookup - looks for the item in the cache 
 * (if max cache size will be exceeded)
 */
/* $begin lookup */
entry_t* lookup (queue_t Q, char* uri)
{
  size_t n = queue_size(Q);
  size_t j;
  list* start = Q->front;

  for (j = 0; j < n; j++)
  {
    if (!strcmp(((entry_t*)(start->data))->uri, uri))
    { 
      return (entry_t*)(start->data);
    } 
    start = start->next;
  }  
  return NULL; 
}
/* $end lookup */


/*
 * addto_cache - checks for vlidity of enq 
 * (if max cache size will be exceeded)
 */
/* $begin addto_cache */
void addto_cache (char* buf, char* url, int siz)
{
  entry_t* new;

  new = malloc(sizeof(entry_t));
  strcpy(new->uri, url);
  new->addr = buf;
  new->size = siz;

  if (queue_size(cache) == CACHE_LINES)
    deq(cache);

  enq(cache, (void*)new);
  return;
}
/* $end addto_cache */


/*
 * main - mostly from tiny.c
 */
/* $begin Proxymain */
int main(int argc, char **argv) 
{
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;
    
    Signal(SIGPIPE, ignore);

    /* Check command line args */
    if (argc != 2) {
      fprintf(stderr, "usage: %s <port>\n", argv[0]);
      exit(1);
        }
    
    cache = queue_new();
    Sem_init(&w, 0, 1);
    Sem_init(&mutex, 0, 1);

    listenfd = Open_listenfd(argv[1]);

    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
              getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                          port, MAXLINE, 0);
              printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
      }
}
/* $end Proxymain */


/*
 * thread - Run thread in “detached” mode. 
 * Runs independently of other threads 
 * Reapedautomatically (by kernel) when it terminate
 * < thread function is from lecture notes >
 */
/* $begin thread */
void *thread(void *vargp) 
{ 
  int connfd = *((int *)vargp); 
  Pthread_detach(pthread_self()); 
  Free(vargp);                   
  doit(connfd); 
  close(connfd);
  return NULL; 
}
/* $end thread */


/*
 * ignore - ignores signals
 */
/* $begin ignore */
void ignore (int sig)
{
  return;
}
/* $end ignore */


/*
 * doit - handle one HTTP request/response transaction
 */
/* $begin doit */
void doit(int fd) 
{
    int clientfd;
    entry_t* entri;
    //char* addr;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char host[MAXLINE], port[MAXLINE], path[MAXLINE];
    rio_t rio;
    rio_t Hrio;
    char toHost[MAXLINE];
    char headersbuf[MAXLINE];
    

    /* Read request line and headers */
    rio_readinitb(&rio, fd);
    if (!rio_readlineb(&rio, buf, MAXLINE))  //line:netp:doit:readrequest
      {
        close(fd);
        return;
      }  
    sscanf(buf, "%s %s %s", method, uri, version);       //line:netp:doit:parserequest
    if (strcasecmp(method, "GET")) {                     //line:netp:doit:beginrequesterr
        clienterror(fd, method, "501", "Not Implemented",
                    "Proxy does not implement this method");
        close(fd);
        return;
      }        
                                                  //line:netp:doit:endrequesterr
    /* Parse UrI from GET request */
    strcpy(port, "");
    strcpy(host, "");
    strcpy(path, "");
    if (!parse_uri(uri, host, port, path)) 
    {
      close(fd);
      return;       //line:netp:doit:staticcheck
    }
    
    P(&mutex); 
    readcnt++;  
    /* First in */ 
    if (readcnt == 1) P(&w); 
    V(&mutex);
    
    //looking up objects in the cache
    entri = lookup(cache, uri);
    if (entri)
    {  
      rio_writen(fd, (entri->addr), (entri->size)); 
      P(&w);
      enq(cache, queue_remove(cache, (entri->uri)));    
      V(&w);
      close(fd);
      return;
    }   
    
    P(&mutex); 
    readcnt--; 
    /* Last out */
    if (readcnt == 0)  V(&w); 
    V(&mutex);


    //open connection with the host
    if ((clientfd = open_clientfd(host, port)) < 0)
    {
      close(fd);
      return;
    } 
    
    rio_readinitb(&Hrio, clientfd);

    //format the host name
    strcpy(toHost, "GET ");
    strcat(toHost, path);
    strcat(toHost, " HTTP/1.0\r\n");

    //send it to the host
    rio_writen(clientfd, toHost, strlen(toHost));

    strcpy(headersbuf, "\0");
    
    read_requesthdrs(&rio, headersbuf);                   //line:netp:doit:readrequesthdrs
    send_requesthdrs(clientfd, headersbuf, host);
    
    receivefH(&Hrio, fd, uri);
    close(clientfd);    
}
/* $end doit */



/*
 * receivefH - reveive HTTP requests responses from the host
 * and send back to the client
 */
/* $begin receivefH */
void receivefH(rio_t *Hrio, int fd, char* uri)
{
  int n;
  int c;
  char buf[MAXLINE];
  char* pre_cache = malloc(MAX_OBJECT_SIZE); 

  while((n = rio_readnb(Hrio, buf, MAXLINE)) > 0) //line:netp:readhdrs:checkterm
    {         
      rio_writen(fd, buf, n);
      c+=n;
      if (c <= MAX_OBJECT_SIZE)
        memcpy(&pre_cache[c-n], buf, n);
    }

  // we should not save any page that is bigger than MAX_OBJECT_SIZE
  if (c <= MAX_OBJECT_SIZE)
    {
      P(&w);
      addto_cache (pre_cache, uri, c);
      V(&w);
    }

  return;
}
/* $end receivefH */



/*
 * read_requesthdrs - read HTTP request headers
 */
/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp, char* headersbuf) 
{
    char buf[MAXLINE];

    //strcpy(buf, "\0");

    while(strcmp(buf, "\r\n")) //line:netp:readhdrs:checkterm
      {          
      rio_readlineb(rp, buf, MAXLINE);
      if(!strstr(buf, "Connection:") && !strstr(buf, "Proxy-Connection:") 
                      && !strstr(buf, "User-Agent:"))
        {
          strcat(headersbuf, buf);
        }
      }
    return;
}
/* $end read_requesthdrs */



/*
 * send_requesthdrs - send HTTP request headers to the host
 */
/* $begin send_requesthdrs */
void send_requesthdrs(int clientfd, char* headersbuf, char* host)
  {
    char hostHdr[MAXLINE];

    rio_writen(clientfd, "Connection: close\r\n", 
      strlen("Connection: close\r\n"));
    rio_writen(clientfd, "Proxy-Connection: close\r\n", 
      strlen("Proxy-Connection: close\r\n"));

    if (!strstr(headersbuf, "Host"))
      {
        strcpy(hostHdr, "");
        strcat(hostHdr, "Host: ");
        strcat(hostHdr, host);
        strcat(hostHdr, "\r\n");
        rio_writen(clientfd, hostHdr, strlen(hostHdr));
      }
    
    rio_writen(clientfd, (void*)user_agent_hdr, strlen(user_agent_hdr));
    rio_writen(clientfd, headersbuf, strlen(headersbuf));

    return;
  }
/* $end send_requesthdrs */



/*
 * parse_uri - parse UrI into host, portnum, and path
 *             return 0 if it's invalid, 1 if valid
 */
/* $begin parse_uri */
int parse_uri(char *uri, char *host, char *port, char *path)
{
  char *http; // pointer to the first occurance of http://
  char *semi; // pointer to the : after the hostname
  char *slash; // pointer to / after hostname
  char *hostloc; //stores http + 7 as a start of hostname location
  int n, m, k, len, slen;
  
  if((len = strlen(uri)) <= 7) return 0; //length of the uri

  if (!(http = strstr(uri, "http://")))
    return 0;
        
  hostloc = http + 7; // http:// has length of 7, start of host
  semi = strstr(hostloc, ":"); // pointer to the :, start of port
  slash = strstr(hostloc, "/"); // pointer to the /, start of path
  
  if (slash)
  {
    strcpy(path, slash); // get the path  
    int plen = strlen(slash); //length of everything in path 
    
    if (semi)
      {
      slen = strlen(semi); // length of port + path
      n = len - slen - 7; //length of the hostname     
      strncpy(host, hostloc, n); //hostname starts after http 
      host[n] = '\0';  
      m = slen - plen - 1; // length of the port number
      strncpy(port, semi + 1, m); //copy the port into portnum
      port[m] = '\0';
      } 

    else 
      {
        strcpy(port, "80");  // default port         
        k = len - plen - 7;  //length of the hostname 
        strncpy(host, hostloc, k); //copy the hostname
        host[k] = '\0';
      }

    return 1;        
  }
 
 if (semi)
  {
    strcpy(port, semi + 1);
    slen = strlen(semi); // length of port + path
    n = len - slen - 7; //length of the hostname     
    strncpy(host, hostloc, n); //hostname starts after http   
  }

 else
  {
    strcpy(port, "80");  // default port 
    strcpy(host, hostloc); //the host  
  }

 strcpy(path, "/");  // default path  
 return 1;
}
/* $end parse_uri*/


/*
 * clienterror - returns an error message to the client
 * function from tiny
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
     char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
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
/* $end clienterror */