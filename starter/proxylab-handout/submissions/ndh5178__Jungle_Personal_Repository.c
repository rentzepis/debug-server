#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_OBJS_COUNT 10

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int connectfd);
void parse_uri(char *uri, char *host, char *port, char *path);
void build_http_header(char *http_header, char *host, char *port, char *path, rio_t *client_rio);
void *thread(void *vargp);
void cache_init(void);
int cache_find(char *uri, char *object, int *size);
void cache_insert(char *uri, char *object, int size);

typedef struct {
  int valid;
  int size;
  int lru;
  char uri[MAXLINE];
  char object[MAX_OBJECT_SIZE];
} cache_block;

cache_block cache[CACHE_OBJS_COUNT];
sem_t cache_mutex;
int cache_time = 0;

int main(int argc, char **argv)
{
  int listenfd,*connectfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  if(argc!=2){
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  cache_init();
  listenfd=Open_listenfd(argv[1]);
  printf("Welcome Proxy \n");
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connectfd = Malloc(sizeof(int));
    *connectfd=Accept(listenfd, (SA *)&clientaddr,&clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, &hostname[0], MAXLINE, &port[0], MAXLINE,0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connectfd);
  }
  return 0;
}

void *thread(void *vargp)
{
    int connectfd = *((int *)vargp);

    Pthread_detach(pthread_self());
    Free(vargp);

    doit(connectfd);
    Close(connectfd);

    return NULL;
}

void doit(int connectfd)
{
  int serverfd;
  ssize_t n;
  int object_size = 0;
  int can_cache = 1;
  int cached_size;
  char buf[MAXBUF];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char uri_key[MAXLINE];
  char host[MAXLINE], port[MAXLINE], path[MAXLINE];
  char http_header[MAXBUF];
  char object_buf[MAX_OBJECT_SIZE];
  char cached_obj[MAX_OBJECT_SIZE];
  rio_t client_rio;
  rio_t server_rio;

  Rio_readinitb(&client_rio, connectfd);

  if (!Rio_readlineb(&client_rio, buf, MAXLINE))return;

  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))return;

  strcpy(uri_key, uri);

  if (cache_find(uri_key, cached_obj, &cached_size)) {
    Rio_writen(connectfd, cached_obj, cached_size);
    return;
  }

  parse_uri(uri, host, port, path);

  build_http_header(http_header, host, port, path, &client_rio);

  serverfd = Open_clientfd(host, port);

  Rio_writen(serverfd, http_header, strlen(http_header));
  Rio_readinitb(&server_rio, serverfd);

  while ((n = Rio_readnb(&server_rio, buf, MAXBUF)) > 0) {
    Rio_writen(connectfd, buf, n);
    if (can_cache) {
      if (object_size + n <= MAX_OBJECT_SIZE) {
        memcpy(object_buf + object_size, buf, n);
        object_size += n;
      }
      else {
        can_cache = 0;
      }
    }
  }

  if (can_cache) {
    cache_insert(uri_key, object_buf, object_size);
  }

  Close(serverfd);
}

void parse_uri(char *uri, char *host, char *port, char *path)
{
  char *host_begin;
  char *host_end;
  char *port_begin;
  char *path_begin;
  int host_len;
  int port_len;

  if (strstr(uri, "http://") == uri)host_begin = uri + strlen("http://");
  else host_begin = uri;

  port_begin = strchr(host_begin, ':');
  path_begin = strchr(host_begin, '/');

  if (port_begin && (!path_begin || port_begin < path_begin)) {
      host_end = port_begin;
      host_len = host_end - host_begin;

      strncpy(host, host_begin, host_len);
      host[host_len] = '\0';

      port_begin++;

      if (path_begin)
          port_len = path_begin - port_begin;
      else
          port_len = strlen(port_begin);

      strncpy(port, port_begin, port_len);
      port[port_len] = '\0';
  } else {
      if (path_begin)
          host_len = path_begin - host_begin;
      else
          host_len = strlen(host_begin);

      strncpy(host, host_begin, host_len);
      host[host_len] = '\0';

      strcpy(port, "80");
  }

  if (path_begin)
      strcpy(path, path_begin);
  else
      strcpy(path, "/");
}

void build_http_header(char *http_header, char *host, char *port, char *path, rio_t *client_rio)
{
  char buf[MAXLINE];
  char request_hdr[MAXLINE];
  char host_hdr[MAXLINE];
  char other_hdr[MAXBUF];

  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);
  strcpy(host_hdr, "");
  strcpy(other_hdr, "");

  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (!strcmp(buf, "\r\n")) {
      break;
    }

    if (!strncasecmp(buf, "Host:", 5)) {
      strcpy(host_hdr, buf);
    }
    else if (!strncasecmp(buf, "Connection:", 11)) {
      continue;
    }
    else if (!strncasecmp(buf, "Proxy-Connection:", 17)) {
      continue;
    }
    else if (!strncasecmp(buf, "User-Agent:", 11)) {
      continue;
    }
    else {
      strcat(other_hdr, buf);
    }
  }

  if (strlen(host_hdr) == 0) {
    if (!strcmp(port, "80")) {
      sprintf(host_hdr, "Host: %s\r\n", host);
    }
    else {
      sprintf(host_hdr, "Host: %s:%s\r\n", host, port);
    }
  }

  sprintf(http_header, "%s%s%s%s%s%s\r\n",
          request_hdr,
          host_hdr,
          user_agent_hdr,
          "Connection: close\r\n",
          "Proxy-Connection: close\r\n",
          other_hdr);
}

void cache_init(void)
{
  int i;

  Sem_init(&cache_mutex, 0, 1);
  for (i = 0; i < CACHE_OBJS_COUNT; i++) {
    cache[i].valid = 0;
    cache[i].size = 0;
    cache[i].lru = 0;
    cache[i].uri[0] = '\0';
  }
}

int cache_find(char *uri, char *object, int *size)
{
  int i;

  P(&cache_mutex);
  for (i = 0; i < CACHE_OBJS_COUNT; i++) {
    if (cache[i].valid && !strcmp(cache[i].uri, uri)) {
      *size = cache[i].size;
      memcpy(object, cache[i].object, cache[i].size);
      cache[i].lru = ++cache_time;
      V(&cache_mutex);
      return 1;
    }
  }
  V(&cache_mutex);
  return 0;
}

void cache_insert(char *uri, char *object, int size)
{
  int i;
  int target = 0;

  if (size > MAX_OBJECT_SIZE) {
    return;
  }

  P(&cache_mutex);

  for (i = 0; i < CACHE_OBJS_COUNT; i++) {
    if (cache[i].valid && !strcmp(cache[i].uri, uri)) {
      target = i;
      break;
    }
    if (!cache[i].valid) {
      target = i;
      break;
    }
    if (cache[i].lru < cache[target].lru) {
      target = i;
    }
  }

  cache[target].valid = 1;
  cache[target].size = size;
  cache[target].lru = ++cache_time;
  strcpy(cache[target].uri, uri);
  memcpy(cache[target].object, object, size);

  V(&cache_mutex);
}
