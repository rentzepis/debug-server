#include "csapp.h"
#include <stdio.h>
#include <strings.h>
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE 10
#define SBUFSIZE 16
#define NTHREADS 4
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

// 实现一个线程池
typedef struct {
  int *buf;
  int n;
  int front;   // 数组第一项
  int rear;    // 数组最后一项
  sem_t mutex; // 提供互斥缓冲区访问
  sem_t slots; // 代表空槽的个数
  sem_t items; // 可用项目的个数
} sbuf_t;
sbuf_t sbuf;
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp, int item);
int sbuf_remove(sbuf_t *sp);
void *thread(void *vargp);

// 实现缓存
typedef struct {
  char uri[MAXLINE];
  char obj[MAX_OBJECT_SIZE];
  int LRU;
  int isEmpty;
  int read_cnt;
  sem_t mutex;
  sem_t w;
} block;
typedef struct {
  block data[MAX_CACHE];
  int num;
} Cache;
Cache cache;
void init_Cache();
int get_Cache(char *uri);
int get_index();
void write_Cache(char *uri, char *buf);
void update_LRU(int index);

struct Uri {
  char host[MAXLINE];
  char port[MAXLINE];
  char path[MAXLINE];
};
void build_header(char *http_header, struct Uri *uri_data, rio_t *client_rio);
void parse_uri(char *uri, struct Uri *uri_data);
void doit(int fd);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  init_Cache();
  if (argc != 2) {
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }
  listenfd = Open_listenfd(argv[1]);
  sbuf_init(&sbuf, SBUFSIZE);
  for (int i = 0; i < NTHREADS; i++) {
    Pthread_create(&tid, NULL, thread, NULL);
  }
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    sbuf_insert(&sbuf, connfd);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
  }
  return 0;
}

void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char server[MAXLINE];
  char cache_tag[MAXLINE];
  rio_t rio, server_rio;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s", method, uri, version);
  strcpy(cache_tag, uri);

  if (strcasecmp(method, "GET")) {
    printf("501 Not Implemented\n");
    return;
  }
  struct Uri *uri_data = (struct Uri *)malloc(sizeof(struct Uri));
  int i;
  if ((i = get_Cache(cache_tag)) != -1) {
    P(&cache.data[i].mutex);
    cache.data[i].read_cnt++;
    if (cache.data[i].read_cnt == 1) {
      P(&cache.data[i].w);
    }
    V(&cache.data[i].mutex);

    Rio_writen(fd, cache.data[i].obj, strlen(cache.data[i].obj));

    P(&cache.data[i].mutex);
    cache.data[i].read_cnt--;
    if (cache.data[i].read_cnt == 0) {
      V(&cache.data[i].w);
    }
    V(&cache.data[i].mutex);
    return;
  }

  parse_uri(uri, uri_data);
  build_header(server, uri_data, &rio);
  int serverfd = Open_clientfd(uri_data->host, uri_data->port);
  if (serverfd < 0) {
    printf("Connection failed\n");
    return;
  }
  Rio_readinitb(&server_rio, serverfd);
  Rio_writen(serverfd, server, strlen(server));

  char cache_buf[MAX_OBJECT_SIZE];
  int size_buf = 0;
  size_t n;
  while ((n = Rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
    size_buf += n;
    if (size_buf < MAX_OBJECT_SIZE) {
      strcat(cache_buf, buf);
    }
    printf("proxy received %d bytes,then send\n", (int)n);
    Rio_writen(fd, buf, n);
  }
  Close(serverfd);
  if (size_buf < MAX_OBJECT_SIZE) {
    write_Cache(cache_tag, cache_buf);
  }
}
void build_header(char *http_header, struct Uri *uri_data, rio_t *client_rio) {
  char *User_Agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) "
                     "Gecko/20120305 Firefox/10.0.3\r\n";
  char *conn_hdr = "Connection: close\r\n";
  char *prox_hdr = "Proxy-Connection: close\r\n";
  char *host_hdr_format = "Host: %s\r\n";
  char *requestlint_hdr_format = "GET %s HTTP/1.0\r\n";
  char *endof_hdr = "\r\n";

  char buf[MAXLINE], request_hdr[MAXLINE], other_hdr[MAXLINE],
      host_hdr[MAXLINE];

  sprintf(request_hdr, requestlint_hdr_format, uri_data->path);
  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (strcmp(buf, endof_hdr) == 0) {
      break;
    }
    if (!strncasecmp(buf, "Host", 4)) {
      strcpy(host_hdr, buf);
      continue;
    }
    if (!strncasecmp(buf, "Connection", strlen("Connection")) &&
        !strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) &&
        !strncasecmp(buf, "User-Agent", strlen("User-Agent"))) {
      strcat(other_hdr, buf);
    }
  }
  if (strlen(host_hdr) == 0) {
    sprintf(host_hdr, host_hdr_format, uri_data->host);
  }
  sprintf(http_header, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr,
          prox_hdr, User_Agent, other_hdr, endof_hdr);
  return;
}
void parse_uri(char *uri, struct Uri *uri_data) {
  char *hostpose = strstr(uri, "//");
  if (hostpose == NULL) {
    char *pathpose = strstr(uri, "/");
    if (pathpose != NULL) {
      strcpy(uri_data->path, pathpose);
    }
    strcpy(uri_data->port, "80");
    return;
  } else {
    char *portpose = strstr(hostpose + 2, ":");
    if (portpose != NULL) {
      int tmp;
      sscanf(portpose + 1, "%d%s", &tmp, uri_data->path);
      sprintf(uri_data->port, "%d", tmp);
      *portpose = '\0';
    } else {
      char *pathpose = strstr(hostpose + 2, "/");
      if (pathpose != NULL) {
        strcpy(uri_data->path, pathpose);
        strcpy(uri_data->port, "80");
        *pathpose = '\0';
      }
    }
    strcpy(uri_data->host, hostpose + 2);
  }
}

// sbuf
void sbuf_init(sbuf_t *sp, int n) {
  sp->buf = (int *)Malloc(n * sizeof(int));
  sp->n = n;
  sp->front = 0;
  sp->rear = 0;
  Sem_init(&sp->mutex, 0, 1);
  Sem_init(&sp->slots, 0, n);
  Sem_init(&sp->items, 0, 0);
}
void sbuf_deinit(sbuf_t *sp) { free(sp->buf); }
void sbuf_insert(sbuf_t *sp, int item) {
  P(&sp->slots);
  P(&sp->mutex);
  sp->buf[(++sp->rear) % sp->n] = item;
  V(&sp->mutex);
  V(&sp->items);
}
int sbuf_remove(sbuf_t *sp) {
  int item;
  P(&sp->items);
  P(&sp->mutex);
  item = sp->buf[(++sp->front) % sp->n];
  V(&sp->mutex);
  V(&sp->slots);
  return item;
}
void *thread(void *vargp) {
  Pthread_detach(pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);
    Close(connfd);
  }
}

// Cache
void init_Cache() {
  cache.num = 0;
  for (int i = 0; i < MAX_CACHE; i++) {
    cache.data[i].isEmpty = 1; // 1表示空 0表示不空
    cache.data[i].LRU = 0;
    Sem_init(&cache.data[i].mutex, 0, 1);
    Sem_init(&cache.data[i].w, 0, 1);
    cache.data[i].read_cnt = 0;
  }
} // 初始化缓存
int get_Cache(char *uri) {
  int i;
  for (i = 0; i < MAX_CACHE; i++) {
    P(&cache.data[i].mutex);
    cache.data[i].read_cnt++;
    if (cache.data[i].read_cnt == 1) {
      P(&cache.data[i].w);
    }
    V(&cache.data[i].mutex);

    if (cache.data[i].isEmpty == 0 && (strcmp(cache.data[i].uri, uri) == 0)) {
      P(&cache.data[i].mutex);
      cache.data[i].read_cnt--;
      if (cache.data[i].read_cnt == 0) {
        V(&cache.data[i].w);
      }
      V(&cache.data[i].mutex);
      break;
    }

    P(&cache.data[i].mutex);
    cache.data[i].read_cnt--;
    if (cache.data[i].read_cnt == 0) {
      V(&cache.data[i].w);
    }
    V(&cache.data[i].mutex);
  }
  if (i >= MAX_CACHE) {
    return -1;
  }
  return i;
} // 查找缓存 如果缓存中没有 返回-1 否则返回下标
int get_index() {
  int min = __INT_MAX__;
  int minindex = 0;
  int i;
  for (i = 0; i < MAX_CACHE; i++) {
    P(&cache.data[i].mutex);
    cache.data[i].read_cnt++;
    if (cache.data[i].read_cnt == 1) {
      P(&cache.data[i].w);
    }
    V(&cache.data[i].mutex);

    if (cache.data[i].isEmpty == 1) {
      minindex = i;
      P(&cache.data[i].mutex);
      cache.data[i].read_cnt--;
      if (cache.data[i].read_cnt == 0) {
        V(&cache.data[i].w);
      }
      V(&cache.data[i].mutex);
      break;
    }
    if (cache.data[i].LRU < min) {
      minindex = i;
      min = cache.data[i].LRU;
      P(&cache.data[i].mutex);
      cache.data[i].read_cnt--;
      if (cache.data[i].read_cnt == 0) {
        V(&cache.data[i].w);
      }
      V(&cache.data[i].mutex);
      continue;
    }

    P(&cache.data[i].mutex);
    cache.data[i].read_cnt--;
    if (cache.data[i].read_cnt == 0) {
      V(&cache.data[i].w);
    }
    V(&cache.data[i].mutex);
  }
  return minindex;
  // 考虑有空缓存的情况
  // 如果没有空缓存，找出LRU最小的缓存
} // 获取缓存的下标
void update_LRU(int index) {
  for (int i = 0; i < MAX_CACHE; i++) {
    if (i != index && cache.data[i].isEmpty == 0) {
      P(&cache.data[i].mutex);
      cache.data[i].LRU--;
      V(&cache.data[i].mutex);
    }
  }
} // 更新LRU
void write_Cache(char *uri, char *buf) {
  int i = get_index();
  P(&cache.data[i].w);
  strcpy(cache.data[i].uri, uri);
  strcpy(cache.data[i].obj, buf);
  cache.data[i].isEmpty = 0;
  cache.data[i].LRU = __INT_MAX__;
  update_LRU(i);
  V(&cache.data[i].w);
} // 写入缓存
