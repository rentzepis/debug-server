#include "csapp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAXLINE 8192

typedef struct {
  int is_valid;
  char tag[MAXLINE];
  char data[MAX_OBJECT_SIZE];
  long long access_time;
  int read_cnt;
  sem_t read_lock;
  sem_t write_lock;
} cacheline;

#define MAX_CACHE_LINES 10
cacheline Cache[MAX_CACHE_LINES];

sem_t time_mutex;
long long time_stamp = 1;

void init_cache() {
  for (int i = 0; i < MAX_CACHE_LINES; i++) {
    Cache[i].is_valid = 0;
    Cache[i].access_time = 0;
    Cache[i].read_cnt = 0;
    Sem_init(&Cache[i].read_lock, 0, 1);
    Sem_init(&Cache[i].write_lock, 0, 1);
  }
  Sem_init(&time_mutex, 0, 1);
}

void read_in(int i) {
  P(&Cache[i].read_lock);
  if (Cache[i].read_cnt == 0) {
    P(&Cache[i].write_lock);
  }
  Cache[i].read_cnt++;
  V(&Cache[i].read_lock);
}

void read_out(int i) {
  P(&Cache[i].read_lock);
  if (Cache[i].read_cnt == 1) {
    V(&Cache[i].write_lock);
  }
  Cache[i].read_cnt--;
  V(&Cache[i].read_lock);
}

int read_cache(int fd, char *uri) {
  int flag = 0;
  for (int i = 0; i < MAX_CACHE_LINES; i++) {
    read_in(i);
    if (Cache[i].is_valid && !strcmp(uri, Cache[i].tag)) {
      flag = 1;
      P(&time_mutex);
      Cache[i].access_time = time_stamp++;
      V(&time_mutex);
      Rio_writen(fd, Cache[i].data, strlen(Cache[i].data));
    }
    read_out(i);
    if (flag) {
      return 0;
    }
  }
  return -1;
}

void write_cache(char *uri, char *data) {
  int has_empty = -1;
  int lru_evict = 0;
  for (int i = 0; i < MAX_CACHE_LINES; i++) {
    read_in(i);
    if (Cache[i].is_valid == 0) {
      has_empty = i;
    }
    if (Cache[i].access_time < Cache[lru_evict].access_time) {
      lru_evict = i;
    }
    read_out(i);
    if (has_empty != -1) {
      break;
    }
  }
  int write_index = (has_empty == -1) ? lru_evict : has_empty;
  P(&Cache[write_index].write_lock);
  Cache[write_index].is_valid = 1;
  P(&time_mutex);
  Cache[write_index].access_time = time_stamp++;
  V(&time_mutex);
  strcpy(Cache[write_index].tag, uri);
  strcpy(Cache[write_index].data, data);
  V(&Cache[write_index].write_lock);
}

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

typedef struct {
  char host[MAXLINE];
  char port[MAXLINE];
  char path[MAXLINE];
} URI;

void cout_uri_format_error() { printf("wrong uri format\n"); }

void parse_line(URI *req_uri, char *uri) {
  char *host_start = strstr(uri, "://");
  if (host_start == NULL) {
    cout_uri_format_error();
    return;
  }
  host_start += 3;
  char *port_start = strstr(host_start, ":");
  char *path_start = strstr(host_start, "/");
  if (path_start == NULL) {
    cout_uri_format_error();
    return;
  }
  strcpy(req_uri->path, path_start);
  *path_start = '\0';
  if (port_start != NULL) {
    strcpy(req_uri->port, port_start + 1);
    *port_start = '\0';
  } else {
    strcpy(req_uri->port, "80");
  }
  strcpy(req_uri->host, host_start);
  return;
}

void build_req_server(char *req_server, URI *req_uri) {
  sprintf(req_server, "GET %s HTTP/1.0\r\n", req_uri->path);
  sprintf(req_server, "%sHost: %s\r\n", req_server, req_uri->host);
  sprintf(req_server, "%s%s", req_server, user_agent_hdr);
  sprintf(req_server, "%sConnection: close\r\n", req_server);
  sprintf(req_server, "%sProxy-Connection: close\r\n", req_server);
  sprintf(req_server, "%s\r\n", req_server);
}

#define SUBFSIZE 16
#define NTHREADS 4

typedef struct {
  int *buf;
  int n;
  int front;
  int rear;
  sem_t mutex;
  sem_t slots;
  sem_t items;
} sbuf_t;

sbuf_t sbuf;

void sbuf_init(sbuf_t *sp, int n) {
  sp->buf = Calloc(n, sizeof(int));
  sp->n = n;
  sp->front = sp->rear = 0;
  Sem_init(&sp->mutex, 0, 1);
  Sem_init(&sp->slots, 0, n);
  Sem_init(&sp->items, 0, 0);
}

void sbuf_deinit(sbuf_t *sp) { Free(sp->buf); }

void sbuf_insert(sbuf_t *sp, int item) {
  P(&sp->slots);
  P(&sp->mutex);
  sp->buf[(++sp->rear) % (sp->n)] = item;
  V(&sp->mutex);
  V(&sp->items);
}

int sbuf_remove(sbuf_t *sp) {
  P(&sp->items);
  P(&sp->mutex);
  int item = sp->buf[(++sp->front) % (sp->n)];
  V(&sp->mutex);
  V(&sp->slots);
  return item;
}

void doit(int fd) {
  // 初始化rio类函数的缓冲区
  rio_t rio;
  Rio_readinitb(&rio, fd);
  // 读入这一行http请求
  char buf[MAXLINE];
  Rio_readlineb(&rio, buf, MAXLINE);
  // printf("Request headers:\n");
  // printf("%s", buf);
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  // 解析这一行http请求，总共三个部分
  if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
    // printf("HTTP Requset Format Wrong!\n");
    return;
  }
  // 判断是否是GET请求，这个比较函数忽略大小写，get也行
  if (strcasecmp(method, "GET")) {
    printf("method: %s not implemented\n", method);
    return;
  }
  // cache
  if (read_cache(fd, uri) != -1) {
    return;
  }
  char temp_uri[MAXLINE];
  memcpy(temp_uri, uri, strlen(uri));
  // printf("这次请求的url是: %s \n", uri);
  // 至此，已经完成了对客户端请求的解析，接下来要构造出对服务器的请求
  // 首先解析我们的uri，得到host port path
  URI *req_uri = (URI *)malloc(sizeof(URI));
  parse_line(req_uri, uri);
  // 根据我们的信息，构造出真正的发往服务器的请求
  char req_server[MAXLINE];
  build_req_server(req_server, req_uri);
  // 开始连接服务器
  int server_fd = Open_clientfd(req_uri->host, req_uri->port);
  if (server_fd < 0) {
    printf("connection failed\n");
    return;
  }
  // 连接成功，设置缓冲区，将request写入
  rio_t server_rio;
  Rio_readinitb(&server_rio, server_fd);
  Rio_writen(server_fd, req_server, strlen(req_server));
  // 等待服务器的返回，并写入客户端的fd中
  size_t rec_bytes;
  // cache
  int accu_bytes = 0;
  char data[MAX_OBJECT_SIZE];
  while ((rec_bytes = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
    printf("proxy received %d bytes\n", (int)rec_bytes);
    Rio_writen(fd, buf, rec_bytes);
    if (accu_bytes + rec_bytes < MAX_OBJECT_SIZE) {
      strcpy(data + accu_bytes, buf);
      accu_bytes += rec_bytes;
    } else {
      accu_bytes = MAX_OBJECT_SIZE;
    }
  }
  // cache
  if (accu_bytes < MAX_OBJECT_SIZE) {
    write_cache(temp_uri, data);
  }
  Close(server_fd);
}

void *thread(void *vargp) {
  Pthread_detach(Pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);
    Close(connfd);
  }
}

int main(int argc, char **argv) {

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  // 监听请求连接的端口
  Signal(SIGPIPE, SIG_IGN);
  int listenfd = Open_listenfd(argv[1]);

  // 线程池
  sbuf_init(&sbuf, SUBFSIZE);
  pthread_t pid;
  for (int i = 0; i < NTHREADS; i++) {
    Pthread_create(&pid, NULL, thread, NULL);
  }

  init_cache();

  // 与客户端进行连接
  int connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)(&clientaddr), &clientlen);
    sbuf_insert(&sbuf, connfd);
  }

  return 0;
}
