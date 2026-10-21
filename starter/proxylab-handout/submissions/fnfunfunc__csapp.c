#include "csapp.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000 // 1M
#define MAX_OBJECT_SIZE 102400 // 100KB

#define MAX_METHOD_LEN 16
#define HEADER_MAX_NUM 32
#define MAX_HOST_LEN 2048
#define MAX_PORT_LEN 8
#define MAX_PATH_LEN 2048
#define MAX_VERSION_LEN 16
#define MAX_HEADER_NAME_LEN 32
#define MAX_HEADER_VALUE_LEN 256

#define THREAD_NUM 4
#define SBUFSIZE 16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

typedef struct {
  char method[MAX_METHOD_LEN];
  char host[MAX_HOST_LEN];
  char port[MAX_PORT_LEN];
  char path[MAX_PATH_LEN];
  char version[MAX_VERSION_LEN];
} request_line;

typedef struct {
  char name[MAX_HEADER_NAME_LEN];  // The name of request header
  char value[MAX_HEADER_VALUE_LEN]; // The value of request header
} request_header;

typedef struct {
  request_line line;
  request_header headers[HEADER_MAX_NUM];
  int header_num; // The number of headers
} request;

typedef struct {
  int *buf;
  int n; // Maximum numbers of slots
  int front;
  int rear;
  sem_t mutex; // Mutex
  sem_t slots; // Counts available slots
  sem_t items; // Counts availabel items
} sbuf_t;

typedef struct {
  int valid;                      // valid flag
  char request[MAXLINE];          // request -- key
  char response[MAX_OBJECT_SIZE]; // response -- value
  int time_stamp;
} cache_pair;

typedef struct {
  sem_t mutex;
  cache_pair *cache_set;
  int cache_pair_num;
} cache_t;

static void doit(int connfd);
static void parse_url(char *uri, char *host, char *port, char *path);
static void read_request(int fd, request *request);
static void read_request_line(rio_t *rp, char *buf, request_line *request_line);
static void read_request_header(rio_t *rp, char *buf, request_header *header);
static int send_request(request *request);
static void forward_response(int connfd, int targetfd, request *request);

static void sigpipe_handler(int sig);

static void sbuf_init(sbuf_t *sp, int n);
static void sbuf_deinit(sbuf_t *sp);
static void sbuf_insert(sbuf_t *sp, int item);
static int sbuf_remove(sbuf_t *sp);

static void *thread(void *vargp);

static void cache_init(cache_t *cache, int n);
static int cache_find(cache_t *cache, request *request, int fd);
static void cache_insert(cache_t *cache, request *request, char *response);

int cur_time_stamp = 0;
sbuf_t sbuf;
cache_t cache;

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  struct sockaddr_storage clientaddr;
  socklen_t clientlen;
  pthread_t tid;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, sigpipe_handler);
  listenfd = Open_listenfd(argv[1]);

  sbuf_init(&sbuf, SBUFSIZE);
  cache_init(&cache, MAX_CACHE_SIZE / MAX_OBJECT_SIZE);

  for (int i = 0; i < THREAD_NUM; ++i) { // Create work threads
    Pthread_create(&tid, NULL, thread, NULL);
  }

  while (true) {
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    sbuf_insert(&sbuf, connfd); // Add connection to buffer
  }
  sbuf_deinit(&sbuf);
  exit(0);
}

static void sigpipe_handler(int sig) {
  fprintf(stderr, "Receive signal: %d\n", sig);
  perror("Error: connection reset by peer\n");
}

static void *thread(void *vargp) {
  Pthread_detach(Pthread_self());
  while (true) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);
    Close(connfd);
  }
}

static void doit(int connfd) {
  request request;
  read_request(connfd, &request); // read the message from client
  if (!cache_find(&cache, &request, connfd)) {
    int clientfd = send_request(&request);
    forward_response(connfd, clientfd, &request);
    Close(clientfd);
  }
}

static void read_request(int fd, request *request) {
  char buf[MAXLINE];
  rio_t rio;
  Rio_readinitb(&rio, fd);
  read_request_line(&rio, buf, &request->line);

  request_header *header = request->headers;
  request->header_num = 0;
  Rio_readlineb(&rio, buf, MAXLINE);
  while (strcmp(buf, "\r\n")) { // read the headers, until the end line
    read_request_header(&rio, buf, header++);
    request->header_num++;
    Rio_readlineb(&rio, buf, MAXLINE);
  }
}

static void read_request_line(rio_t *rp, char *buf,
                              request_line *request_line) {
  char uri[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  printf("Request: %s\n", buf);
  if (sscanf(buf, "%s %s %s", request_line->method, uri,
             request_line->version) < 3) {
    perror("Error: invalid request line\n");
    exit(1);
  }
  if (strcasecmp(request_line->method, "GET")) { // If the method is not GET
    perror("Error: method not implemented\n");
    exit(1);
  }
  if (strcasecmp(request_line->version, "HTTP/1.0") &&
      strcasecmp(request_line->version, "HTTP/1.1")) {
    perror("HTTP version not recognized\n");
    exit(1);
  }
  parse_url(uri, request_line->host, request_line->port, request_line->path);
}

static void read_request_header(rio_t *rp, char *buf, request_header *header) {
  Rio_readlineb(rp, buf, MAXLINE);
  char *c = strstr(buf, ": ");
  if (c == NULL) {
    fprintf(stderr, "Error: invalid header: %s\n", buf);
    exit(1);
  }
  *c = '\0';
  strcpy(header->name, buf);
  strcpy(header->value, c + 2);
}

static int send_request(request *request) {
  int clientfd;
  char content[MAXLINE];
  rio_t rio;
  request_line *request_line = &request->line;
  clientfd = Open_clientfd(request_line->host, request_line->port);
  Rio_readinitb(&rio, clientfd);
  sprintf(content, "%s %s HTTP/1.0\r\n", request_line->method,
          request_line->path);
  sprintf(content, "%sHost: %s:%s\r\n", content, request_line->host,
          request_line->port);
  sprintf(content, "%s%s\r\n", content, user_agent_hdr);
  sprintf(content, "%sConnection: close\r\n", content);
  sprintf(content, "%sProxy-Connection: close\r\n", content);
  for (int i = 0; i < request->header_num; ++i) {
    request_header header = request->headers[i];
    char *name = header.name;
    char *value = header.value;
    if (!strcasecmp(name, "Host") || !strcasecmp(name, "User-Agent") ||
        !strcasecmp(name, "Connection") ||
        !strcasecmp(name, "Proxy-Connection")) {
      continue;
    }
    sprintf(content, "%s%s: %s\r\n\r\n", content, name, value);
  }
  Rio_writen(clientfd, content, strlen(content));
  return clientfd;
}

static void forward_response(int connfd, int targetfd, request *request) {
  rio_t rio;
  int n;
  char buf[MAXLINE], content[MAX_OBJECT_SIZE];
  int response_bytes = 0;
  Rio_readinitb(&rio, targetfd);
  while ((n = Rio_readlineb(&rio, buf, MAXLINE)) > 0) {
    Rio_writen(connfd, buf, n);
    if (response_bytes + n <= MAX_OBJECT_SIZE) {
      strcpy(content + response_bytes, buf);
    }
    response_bytes += n;
  }
  if (response_bytes <= MAX_OBJECT_SIZE) {
    cache_insert(&cache, request, content);
  }
}

static void parse_url(char *uri, char *host, char *port, char *path) {
  char *hostpose = strstr(uri, "//");
  if (hostpose == NULL) {
    char *pathpose = strstr(uri, "/");
    if (pathpose != NULL) {
      strcpy(path, pathpose);
    }
    strcpy(port, "80"); // The default port is 80
    return;
  } else {
    char *portpose = strstr(hostpose + 2, ":");
    if (portpose != NULL) {
      int portnum;
      sscanf(portpose + 1, "%d%s", &portnum, path);
      sprintf(port, "%d", portnum);
      *portpose = '\0';
    } else {
      char *pathpose = strstr(hostpose + 2, "/");
      if (pathpose != NULL) {
        strcpy(path, pathpose);
        strcpy(port, "80");
        *pathpose = '\0';
      }
    }
    strcpy(host, hostpose + 2);
  }
  return;
}

static void sbuf_init(sbuf_t *sp, int n) {
  sp->buf = (int *)Calloc(n, sizeof(int));
  sp->n = n;
  sp->front = sp->rear = 0;
  Sem_init(&sp->mutex, 0, 1);
  Sem_init(&sp->slots, 0, n);
  Sem_init(&sp->items, 0, 0);
}

static void sbuf_deinit(sbuf_t *sp) { Free(sp->buf); }

static void sbuf_insert(sbuf_t *sp, int item) {
  sem_wait(&sp->slots); // Wait for available slots
  sem_wait(&sp->mutex);
  sp->buf[(++sp->rear) % sp->n] = item;
  sem_post(&sp->mutex);
  sem_post(&sp->items);
}

static int sbuf_remove(sbuf_t *sp) {
  sem_wait(&sp->items); // Wait for available items'
  sem_wait(&sp->mutex);
  int item = sp->buf[(++sp->front) % sp->n];
  sem_post(&sp->mutex);
  sem_post(&sp->slots);
  return item;
}

static void cache_init(cache_t *cache, int n) {
  Sem_init(&cache->mutex, 0, 1);
  cache->cache_set = (cache_pair *)Calloc(n, sizeof(cache_pair));
  cache->cache_pair_num = n;
}

static int cache_find(cache_t *cache, request *request, int fd) {
  request_line *request_line = &request->line;
  char match_str[MAXLINE];
  int cache_hit = 0; // The number of cache hits
  cache_pair *cache_pair;
  sprintf(match_str, "%s http://%s:%s%s HTTP/1.0", request_line->method,
          request_line->host, request_line->port, request_line->path);
  sem_wait(&cache->mutex);
  for (int i = 0; i < cache->cache_pair_num; ++i) {
    cache_pair = cache->cache_set + i;
    if (!cache_pair->valid) { // invalid
      continue;
    }
    if (!strcmp(cache_pair->request, match_str)) {
      Rio_writen(fd, cache_pair->response,
                 MAX_OBJECT_SIZE); // write to the connection of client
      cache_pair->time_stamp = cur_time_stamp++;
      cache_hit = 1;
      break;
    }
  }
  sem_post(&cache->mutex);
  if (cache_hit) {
    printf("Cache hit: http://%s:%s%s\n\n", request_line->host,
           request_line->port, request_line->path);
  }
  return cache_hit;
}

static void cache_insert(cache_t *cache, request *request, char *response) {
  request_line *request_line = &request->line;
  char match_str[MAXLINE];
  cache_pair *cache_pair, *oldest_pair;
  int oldest_time_stamp = 0x7fffffff;
  int vacancy = 0; // Has vacancy???
  sprintf(match_str, "%s http://%s:%s%s HTTP/1.0", request_line->method,
          request_line->host, request_line->port, request_line->path);
  sem_wait(&cache->mutex);
  for (int i = 0; i < cache->cache_pair_num; ++i) {
    cache_pair = cache->cache_set + i;
    if (!cache_pair->valid) {
      strcpy(cache_pair->request, match_str);
      strcpy(cache_pair->response, response);
      cache_pair->valid = 1;
      cache_pair->time_stamp = cur_time_stamp++;
      vacancy = 1;
      break;
    }
    if (oldest_time_stamp > cache_pair->time_stamp) {
      oldest_time_stamp = cache_pair->time_stamp;
      oldest_pair = cache_pair;
    }
  }

  if (!vacancy) {
    strcpy(oldest_pair->request, match_str);
    strcpy(oldest_pair->response, response);
    oldest_pair->time_stamp = cur_time_stamp++;
  }
  sem_post(&cache->mutex);
}