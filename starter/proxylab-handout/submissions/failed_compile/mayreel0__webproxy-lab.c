#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16

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
  struct cache_node *head;
  struct cache_node *tail;
  int size;
  sem_t lock;
} cache;

typedef struct cache_node{
  char *url;
  char *data;
  int size;
  struct cache_node *prev;
  struct cache_node *next;
} cache_node;

cache *cache_init() {
  cache *c = (cache *)malloc(sizeof(cache));
  c->head = NULL;
  c->tail = NULL;
  c->size = 0;
  Sem_init(&c->lock, 0, 1);
  return c;
}

cache_node *cache_has_url(cache *c, char *url) {
  cache_node *cn = c->head;
  while (cn != NULL) {
    if (strcmp(cn->url, url) == 0) {
      V(&c->lock);  
      return cn;
    }
    cn = cn->next;
  }
  V(&c->lock);  
  return NULL;
}

void cache_add(cache *c, char *url, char *data, int size) {
  P(&c->lock); 
  cache_node *cn;
  if ((cn = cache_has_url(c, url))) {
    cache_remove_node(c, cn);
  } else {
    while (c->size + size > MAX_CACHE_SIZE) {
      cache_remove_node(c, c->tail);
    }
  }
  cn = (cache_node *)malloc(sizeof(cache_node));
  cn->url = strdup(url);
  cn->size = size;
  cn->data = malloc(size);
  memcpy(cn->data, data, size);
  cn->next = c->head;
  cn->prev = NULL;

  if (c->head) {
    c->head->prev = cn;
  }
  c->head = cn;
  c->size += size;
  if (!(c->tail)) {
    c->tail = cn;
  }
  V(&c->lock); 
}

void cache_remove_node(cache *c, cache_node *cn) {
  if (c == NULL || cn == NULL) return;
  P(&c->lock);
  if (c->head == cn) {
    c->head = cn->next;
  } else {
    cn->prev->next = cn->next;
  }
  if (c->tail == cn) {
    c->tail = cn->prev;
  } else {
    cn->next->prev = cn->prev;
  }
  c->size -= cn->size;
  free(cn->data);
  free(cn);
  V(&c->lock);
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
  sp->buf[(++sp->rear) % (sp->n)] = item;
  V(&sp->mutex);
  V(&sp->items);
}

int sbuf_remove(sbuf_t *sp)
{
  int item;
  P(&sp->items);
  P(&sp->mutex);
  item = sp->buf[(++sp->front) % (sp->n)];
  V(&sp->mutex);
  V(&sp->slots);
  return item;
}

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
sbuf_t sbuf;
cache *c;

void doit(int fd);
void parse_uri(char *uri, char *server_hostname, char *server_port, char *filename);
void serve_header(int fd, char *method, char *filename, char *server_hostname, char *server_port);
// void read_requesthdrs(rio_t *rp);
// void parse_uri(char *uri, char *filename);
// void serve_static(int fd, char *filename, int filesize, char *hostname);
// void get_filetype(char *filename, char *filetype);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);

int main(int argc, char **argv) {
  int i, listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  pthread_t tid;
  c = cache_init();
  
  struct sockaddr_storage clientaddr;
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  sbuf_init(&sbuf, SBUFSIZE);

  for (i = 0; i < NTHREADS; i++) {
    Pthread_create(&tid, NULL, thread, NULL);
  }

  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("%s %s \n", hostname, port);
    sbuf_insert(&sbuf, connfd);
    // doit(connfd);
    // Close(connfd);
  }
}
void *thread(void *vargp)
{
  Pthread_detach(pthread_self());
  while(1) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);
    Close(connfd);
  }
}

void doit(int clientfd) 
{
  rio_t rio_client, rio_server;
  cache_node *cn;
  int serverfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char server_hostname[MAXLINE], server_port[MAXLINE], filename[MAXLINE];

  Rio_readinitb(&rio_client, clientfd);
  Rio_readlineb(&rio_client, buf, MAXLINE);
  printf("Request headers:\n%s\n", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  
  if (strcasecmp(method, "GET")) {
    clienterror(clientfd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }

  parse_uri(uri, server_hostname, server_port, filename);

  if((cn = cache_has_url(c, filename))) {
    Rio_writen(clientfd, cn->data, cn->size);
    return;
  }

  serverfd = Open_clientfd(server_hostname, server_port);
  if (serverfd < 0) {
    printf("Error connecting to server\n");
    return;
  }
  Rio_readinitb(&rio_server, serverfd);
  serve_header(serverfd, method, filename, server_hostname, server_port);
  char *cache_data = malloc(MAX_OBJECT_SIZE);
  size_t node_size = 0;
  size_t n;

  printf("-------%s %s %s\n", server_hostname, server_port, filename);
  while ((n = Rio_readlineb(&rio_server, buf, MAXLINE)) != 0) {
    memcpy(cache_data + node_size, buf, n);
    node_size += n;
    Rio_writen(clientfd, buf, n);
  }

  printf("-------%s %s %s\n", server_hostname, server_port, filename);

  if (node_size > 0) {
    cache_add(c, filename, cache_data, node_size);
  } else {
    free(cache_data);
  }

  Close(serverfd);
} 

void serve_header(int fd, char *method, char *filename, char *server_hostname, char *server_port) {
  char buf[MAXLINE];

  sprintf(buf, "%s /%s HTTP/1.0\r\n", method, filename);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Host: %s:%s\r\n", server_hostname, server_port);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "User-Agent: %s\r\n", user_agent_hdr);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Connection: close\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Proxy-Connection: close\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));
}

void parse_uri(char *uri, char *server_hostname, char *server_port, char *filename) {
  char *ptr = strstr(uri, "//");
  char *tmp = strchr(ptr, ':');
  ptr = (ptr != NULL) ? ptr + 2 : uri;
  
  if (tmp != NULL) {
    *tmp = '\0'; 
    strcpy(server_hostname, ptr); 
    ptr = tmp + 1; 
  } else {
    strcpy(server_hostname, ptr);
    strcpy(server_port, "80"); 
    return;
  }
  
  tmp = strchr(ptr, '/');
  if (tmp != NULL) {
    *tmp = '\0'; 
    strcpy(server_port, ptr); 
    strcpy(filename, tmp + 1); 
  } else {
    
    strcpy(server_port, ptr);
    strcpy(filename, ""); 
  }
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  sprintf(body, "<html><title>Tiny Error</html></title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));  
}