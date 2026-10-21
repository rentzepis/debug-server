#include "csapp.h"
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Struct for cache block */
typedef struct cache_blk {
  char *uri;
  char *rsp;
  char *content;
  int content_length;
  struct cache_blk *next;
  struct cache_blk *prev;
} cache_blk;

/* Struct for cache */
typedef struct cache_t {
  int size;
  cache_blk *head;
  cache_blk *tail;
} cache_t;

/* Function prototypes */
void doit(int);
int parse_uri(char *, char *, char *, char *);
void convert_reqhdrs(char *, char *, char *);

/* Cache functions */
void init_cache(cache_t *);
cache_blk *find_cache_blk(cache_t *, char *);
void add_cache_blk(cache_t *, char *, char *, char *, int);
void enqueue(cache_t *, char *, char *, char *, int);
void dequeue(cache_t *);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

static const char *server_version = "HTTP/1.0";

static const char *default_port = "80";

static cache_t *proxy_cache;

/* Mutex and readcnt for cache */
sem_t mutex, w;
int readcnt;

/*
 * thread - thread routine
 *
 * input:
 *  targ - file descriptor of connection
 */
void *thread(void *targ) {
  /* Detach thread to avoid memory leak */
  Pthread_detach(Pthread_self());

  /* Get connection file descriptor */
  int connfd = *((int *)targ);
  Free(targ);

  /* Handle request */
  doit(connfd);

  /* Close connection */
  Close(connfd);
  return NULL;
}

int main(int argc, char **argv) {
  int listenfd;
  int *connfd;
  socklen_t clientlen;
  struct sockaddr_in clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  /* Initialize cache */
  Sem_init(&mutex, 0, 1);
  Sem_init(&w, 0, 1);
  readcnt = 0;
  proxy_cache = (cache_t *)Malloc(sizeof(cache_t));
  init_cache(proxy_cache);

  /* Listen to port */
  listenfd = Open_listenfd(argv[1]);
  clientlen = sizeof(clientaddr);
  while (1) {
    /* Accept connection (Malloc to avoid race condition) */
    connfd = Malloc(sizeof(int));
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    /* Create thread */
    Pthread_create(&tid, NULL, thread, connfd);
  }
}

/*
 * doit - handle one HTTP request/response transaction
 *
 * input:
 *  fd - file descriptor of connection
 */
void doit(int fd) {
  char req_buf[MAXLINE], reqhdrs[MAXLINE] = "";
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], path[MAXLINE];
  char rsp_buf[MAXLINE] = "", rsp[MAXLINE] = "";
  int serverfd;
  rio_t proxy_req, server_rsp;
  char *content_buf = NULL;
  int content_length;

  /* Read request line and headers */
  Rio_readinitb(&proxy_req, fd);
  if (!Rio_readlineb(&proxy_req, req_buf, MAXLINE)) {
    return;
  }
  /* Parse request line */
  sscanf(req_buf, "%s %s %s", method, uri, version);
  while (strcmp(req_buf, "\r\n")) {
    /* Read until end of request headers */
    Rio_readlineb(&proxy_req, req_buf, MAXLINE);
    strcat(reqhdrs, req_buf);
  }
  if (strcasecmp(method, "GET")) {
    /* Only implement GET method */
    printf("Proxy does not implement the method\n");
    return;
  }
  if (parse_uri(uri, host, port, path) == -1) {
    /* Only implement http protocol */
    printf("Not http protocol\n");
    return;
  }
  /* Check cache */
  cache_blk *blk = find_cache_blk(proxy_cache, uri);
  if (blk) {
    /* Cache hit */
    Rio_writen(fd, blk->rsp, strlen(blk->rsp));
    Rio_writen(fd, blk->content, blk->content_length);
    return;
  }
  /* Cache miss, connect to server */
  while ((serverfd = Open_clientfd(host, port)) < 0)
    ;

  /* Send request line and headers to server */
  sprintf(req_buf, "%s %s %s\r\n", method, path, server_version);
  Rio_writen(serverfd, req_buf, strlen(req_buf));

  convert_reqhdrs(reqhdrs, host, port);
  Rio_writen(serverfd, reqhdrs, strlen(reqhdrs));

  /* Read response from server and send to client */
  Rio_readinitb(&server_rsp, serverfd);

  do {
    /* Read until end of response headers */
    Rio_readlineb(&server_rsp, rsp_buf, MAXLINE);
    if (!strncasecmp(rsp_buf, "Content-Length:", 15)) {
      /* Get content length */
      sscanf(rsp_buf + 15, "%d\r\n", &content_length);
    }
    strcat(rsp, rsp_buf);
  } while (strcmp(rsp_buf, "\r\n"));
  Rio_writen(fd, rsp, strlen(rsp));

  /* Read content and send to client */
  content_buf = (char *)Malloc(content_length);
  Rio_readnb(&server_rsp, content_buf, content_length);
  Rio_writen(fd, content_buf, content_length);

  /* Add to cache */
  add_cache_blk(proxy_cache, uri, rsp, content_buf, content_length);

  /* Close connection to server */
  Close(serverfd);
}

/*
 * parse_uri - parse URI into host, port and path
 *
 * input:
 *  uri - URI string
 *  host - host string
 *  port - port string
 *  path - path string
 *
 * return:
 *  0 - success
 *  -1 - error
 */
int parse_uri(char *uri, char *host, char *port, char *path) {
  /* Get host */
  char *start = strstr(uri, "http://");
  if (start == NULL) {
    /* Only implement http protocol */
    return -1;
  }
  start += 7;
  char *end = start;
  while (*end != ':' && *end != '/' && *end != '\0') {
    end++;
  }
  strncpy(host, start, end - start);

  if (*end == ':') {
    /* Get port */
    start = ++end;
    while (*end != '/' && *end != '\0') {
      end++;
    }
    strncpy(port, start, end - start);
  } else {
    /* Use default port */
    strncpy(port, default_port, strlen(default_port));
  }
  /* Get path */
  strcpy(path, end);
  return 0;
}

/*
 * convert_reqhdrs - convert request headers
 *
 * input:
 *  reqhdrs: request headers
 *  host: host
 *  port: port
 *
 * output:
 *  reqhdrs: new request headers
 *
 * replace "Host", "User-Agent", "Connection", "Proxy-Connection" headers with
 * new ones
 */
void convert_reqhdrs(char *reqhdrs, char *host, char *port) {
  char new_hdrs[MAXLINE];
  char *line;
  /* Replace headers */
  sprintf(new_hdrs, "Host: %s:%s\r\n%s%s%s", host, port, user_agent_hdr,
          connection_hdr, proxy_connection_hdr);
  /* Copy other headers */
  line = strtok(reqhdrs, "\r\n");
  while (line != NULL) {
    if (strncmp(line, "Host:", 5) != 0 &&
        strncmp(line, "User-Agent:", 11) != 0 &&
        strncmp(line, "Connection:", 11) != 0 &&
        strncmp(line, "Proxy-Connection:", 17) != 0) {
      strcat(new_hdrs, line);
      strcat(new_hdrs, "\r\n");
    }
    line = strtok(NULL, "\r\n");
  }
  /* Add end of headers */
  strcat(new_hdrs, "\r\n");

  /* Copy new headers to reqhdrs */
  strncpy(reqhdrs, new_hdrs, MAXLINE);
}

/*
 * init_cache - initialize cache
 *
 * input:
 *  cache: cache
 */
void init_cache(cache_t *cache) {
  cache->size = 0;
  cache->head = NULL;
  cache->tail = NULL;
}

/*
 * find_cache_blk - find cache block with uri
 *
 * input:
 *  cache: cache
 *  uri: uri
 *
 * return:
 *  cache block with uri
 *
 * if not found, return NULL
 * if found, move cache block to head of cache and return it
 */
cache_blk *find_cache_blk(cache_t *cache, char *uri) {
  /* 1st reader-writer problem(favors readers) */
  P(&mutex);
  readcnt++;
  if (readcnt == 1) {
    /* First reader */
    P(&w);
  }
  V(&mutex);
  /* Find cache block */
  cache_blk *blk = cache->head;
  while (blk != NULL) {
    if (!strcmp(blk->uri, uri)) {
      break;
    }
    blk = blk->next;
  }
  /* 1st reader-writer problem(favors readers) */
  P(&mutex);
  readcnt--;
  if (readcnt == 0) {
    /* Last reader */
    V(&w);
  }
  V(&mutex);
  /* Re-insert cache block */
  if (blk != NULL) {
    /* 1st reader-writer problem(favors readers) */
    P(&w);
    /* Remove cache block */
    if (blk->prev != NULL) {
      blk->prev->next = blk->next;
    } else {
      cache->head = blk->next;
    }
    if (blk->next != NULL) {
      blk->next->prev = blk->prev;
    } else {
      cache->tail = blk->prev;
    }
    /* Insert cache block to the head */
    blk->prev = NULL;
    blk->next = cache->head;
    if (cache->head != NULL) {
      cache->head->prev = blk;
    }
    cache->head = blk;
    /* 1st reader-writer problem(favors readers) */
    V(&w);
  }
  return blk;
}

/*
 * enqueue - enqueue cache block
 *
 * input:
 *  cache: cache
 *  uri: uri
 *  rsp: response headers
 *  content: content
 *  content_length: content length
 *
 * enqueue cache block to the head of cache
 * if cache is empty, enqueue new cache block to the head and tail of cache
 */
void enqueue(cache_t *cache, char *uri, char *rsp, char *content,
             int content_length) {
  /* malloc cache block */
  cache_blk *blk = (cache_blk *)Malloc(sizeof(cache_blk));

  /* copy uri, rsp, content to cache block */
  blk->uri = (char *)Malloc(strlen(uri) + 1);
  strcpy(blk->uri, uri);
  blk->rsp = (char *)Malloc(strlen(rsp) + 1);
  strcpy(blk->rsp, rsp);
  blk->content = content;
  blk->content_length = content_length;
  blk->next = NULL;
  blk->prev = NULL;
  if (cache->size == 0) {
    cache->head = blk;
    cache->tail = blk;
  } else {
    blk->next = cache->head;
    cache->head->prev = blk;
    cache->head = blk;
  }
  /* update cache size */
  cache->size += content_length + strlen(rsp);
}

/*
 * dequeue - dequeue cache block
 *
 * input:
 *  cache: cache
 *
 * dequeue cache block from the tail of cache
 */
void dequeue(cache_t *cache) {
  if (cache->size == 0) {
    /* Cache is empty */
    return;
  }
  /* dequeue cache block */
  cache_blk *blk = cache->tail;
  cache->tail = blk->prev;
  cache->tail->next = NULL;

  /* update cache size */
  cache->size -= blk->content_length + strlen(blk->rsp);

  /* free cache block */
  Free(blk->uri);
  Free(blk->rsp);
  Free(blk->content);
  Free(blk);
}

/*
 * add_cache_blk - add cache block
 *
 * input:
 *  cache: cache
 *  uri: uri
 *  rsp: response headers
 *  content: content
 *  content_length: content length
 *
 * add cache block to the head of cache
 * if cache is full, dequeue cache block until cache is not full
 * if cache block with uri exists, do nothing
 * if content length is greater than MAX_OBJECT_SIZE, do nothing
 */
void add_cache_blk(cache_t *cache, char *uri, char *rsp, char *content,
                   int content_length) {
  /* check if cache block with uri exists */
  cache_blk *blk = find_cache_blk(cache, uri);
  if (content_length > MAX_OBJECT_SIZE) {
    /* Content length is greater than MAX_OBJECT_SIZE */
    Free(content);
    return;
  } else if (blk != NULL) {
    /* Cache block with uri exists */
    Free(content);
    return;
  }
  /* 1st reader-writer problem(favors readers) */
  P(&w);
  while (cache->size + content_length > MAX_CACHE_SIZE) {
    /* Cache is full */
    dequeue(cache);
  }
  /* Add cache block */
  enqueue(cache, uri, rsp, content, content_length);
  /* 1st reader-writer problem(favors readers) */
  V(&w);
}
