#include <fcntl.h>
#include <stdio.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include "csapp.h"
#include "defs.h"
#include "sbuf.h"
#include "scache.h"

/* Recommended max cache and object sizes */
#define CLEAR_BUF(x) (memset((x), 0, sizeof((x))))

typedef struct {
  scache_t cache;
  sbuf_t sbuf;
} thread_args;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";
static const char *host_hdr_format = "Host: %s\r\n";
static const char *requestlint_hdr_format = "%s %s HTTP/1.0\r\n";
static const char *endof_hdr = "\r\n";

static const char *connection_key = "Connection";
static const char *user_agent_key = "User-Agent";
static const char *proxy_connection_key = "Proxy-Connection";
static const char *host_key = "Host";
static const int http_port = 80;
static const int thread_num = 16;
static const int shared_buffer_size = 1024;

void doit(int connfd, scache_t *cache);

void parse_uri(char *uri, char *hostname, char *path, int *port);

void build_http_request(char *http_header, char *method, char *hostname,
                        char *path, rio_t *client_rio);

int connect_server(char *hostname, int port);

void *thread_function(void *s) {
  Pthread_detach(Pthread_self());
  thread_args *args = (thread_args *)s;

  while (1) {
    doit(sbuf_remove(&args->sbuf), &args->cache);
  }

  return NULL;
}

// 代理介于服务器和客户端之间，对于服务器是客户端，对于客户端是服务器。
int main(int argc, char **argv) {
  int errfd;
  int listenfd;
  pthread_t threads[thread_num];
  thread_args args;
  int connfd;

  errfd = open("./err.out", O_WRONLY | O_APPEND | O_CREAT, S_IRWXU);
  Dup2(errfd, STDERR_FILENO);

  if (argc != 2) {
    fprintf(stderr, "usage :%s <port> \n", argv[0]);
    return 1;
  }

  listenfd = Open_listenfd(argv[1]);

  if (listenfd < 0) {
    sprintf(stderr, "invalid port.\n");
    return 1;
  }

  sbuf_init(&args.sbuf, shared_buffer_size);
  scache_init(&args.cache);

  for (int i = 0; i < thread_num; ++i) {
    Pthread_create(&threads[i], NULL, thread_function, (void *)&args);
  }

  Signal(SIGPIPE, SIG_IGN);

  fprintf(stderr, "start\n");
  while (1) {
    connfd = Accept(listenfd, NULL, NULL);

    if (connfd >= 0) {
      sbuf_insert(&args.sbuf, connfd);
    }
  }

  sbuf_deinit(&args.sbuf);
  scache_deinit(&args.cache);
  return 0;
}

void doit(int connfd, scache_t *cache) {
  char http_response[MAX_OBJECT_SIZE + 1];  // 从服务器发来的http响应。
  char http_request[MAXLINE];  // 从客户端发出去的http请求。

  char request_method[MAXLINE >> 3];
  char request_uri[MAXLINE >> 3];
  char request_http_version[MAXLINE >> 3];
  char request_http_header[MAXLINE >> 3];
  char request_hostname[MAXLINE >> 3];
  char request_path[MAXLINE >> 3];

  int server_port;
  int server_fd;
  int response_size;
  int uri_size;

  // 因为每次客户端请求的服务器不一定一样，所以记录上一次服务器的fd，不一样则关闭。
  int last_server_fd = -1;
  rio_t client_rio, server_rio;

  CLEAR_BUF(http_response);
  CLEAR_BUF(http_request);
  CLEAR_BUF(request_method);
  CLEAR_BUF(request_uri);
  CLEAR_BUF(request_http_version);
  CLEAR_BUF(request_http_header);
  CLEAR_BUF(request_hostname);
  CLEAR_BUF(request_path);

  Rio_readinitb(&client_rio, connfd);

  // 循环，让客户端和服务器进行交互，每次发送一个http请求，接受一个http相应。
  while (Rio_readlineb(&client_rio, request_http_header,
                       sizeof(request_http_header)) > 0) {
    // 处理http的请求头。
    sscanf(request_http_header, "%s %s %s", request_method, request_uri,
           request_http_version);

    // 先处理 GET 头。
    if (strcasecmp(request_method, "GET")) {
      printf("Proxy does not implement the method");
      break;
    }

    // 解析uri。
    parse_uri(request_uri, request_hostname, request_path, &server_port);
    uri_size = strlen(request_uri);

    if (cache == NULL || scache_found(cache, request_uri, uri_size,
                                      http_response, &response_size) == 0) {
      // 一次性把客户端发的这次的http请求发送给服务器。组合成http请求。
      build_http_request(http_request, request_method, request_hostname,
                         request_path, &client_rio);

      server_fd = connect_server(request_hostname, server_port);

      // 因为每次请求的服务器不一定相等，所以可能需要释放上次的。
      if (last_server_fd > 0 && last_server_fd != server_fd) {
        Close(last_server_fd);
      }

      if (server_fd < 0) {
        printf("connection failed\n");
        break;
      }

      last_server_fd = server_fd;

      Rio_readinitb(&server_rio, server_fd);
      Rio_writen(server_fd, http_request, strlen(http_request));

      char *http_ptr = http_response;
      while ((response_size = Rio_readlineb(&server_rio, http_ptr, MAXLINE)) !=
             0) {
        Rio_writen(connfd, http_ptr, response_size);
        http_ptr += response_size;
      }

      if (cache != NULL) {
        scache_insert(cache, request_uri, uri_size, http_response,
                      http_ptr - http_response);
      }
    } else {
      Rio_writen(connfd, http_response, response_size);
    }

    CLEAR_BUF(http_response);
    CLEAR_BUF(http_request);
    CLEAR_BUF(request_method);
    CLEAR_BUF(request_uri);
    CLEAR_BUF(request_http_version);
    CLEAR_BUF(request_http_header);
    CLEAR_BUF(request_hostname);
    CLEAR_BUF(request_path);
  }

  if (server_fd > 0) {
    Close(server_fd);
  }

  Close(connfd);
}

void build_http_request(char *http_request, char *method, char *hostname,
                        char *path, rio_t *client_rio) {
  char buf[MAXLINE];
  char request_hdr[MAXLINE >> 3];
  char other_hdr[MAXLINE >> 3];
  char host_hdr[MAXLINE >> 3];

  CLEAR_BUF(buf);
  CLEAR_BUF(request_hdr);
  CLEAR_BUF(other_hdr);
  CLEAR_BUF(host_hdr);

  sprintf(request_hdr, requestlint_hdr_format, method, path);

  while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
    if (strcmp(buf, endof_hdr) == 0) {
      break;
    }

    if (!strncasecmp(buf, host_key, strlen(host_key))) {
      strcpy(host_hdr, buf);
      continue;
    }

    if (!strncasecmp(buf, connection_key, strlen(connection_key)) &&
        !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key)) &&
        !strncasecmp(buf, user_agent_key, strlen(user_agent_key))) {
      strcat(other_hdr, buf);
    }

    CLEAR_BUF(buf);
  }

  if (strlen(host_hdr) == 0) {
    sprintf(host_hdr, host_hdr_format, hostname);
  }

  sprintf(http_request, "%s%s%s%s%s%s%s", request_hdr, host_hdr, conn_hdr,
          prox_hdr, user_agent_hdr, other_hdr, endof_hdr);
  return;
}

int connect_server(char *hostname, int port) {
  char port_str[MAXLINE >> 7];
  sprintf(port_str, "%d", port);
  return Open_clientfd(hostname, port_str);
}

void parse_uri(char *uri, char *hostname, char *path, int *port) {
  *port = http_port;
  char *host;
  char *skip_header;
  char t;

  // http://hostname(:hostport)/path
  skip_header = strstr(uri, "//");
  skip_header = skip_header != NULL ? skip_header + 2 : uri;
  host = strstr(skip_header, ":");

  if (host != NULL) {
    t = *host;
    *host = '\0';
    sscanf(skip_header, "%s", hostname);
    sscanf(host + 1, "%d%s", port, path);
    *host = t;
  } else {
    host = strstr(skip_header, "/");
    if (host != NULL) {
      t = *host;
      *host = '\0';
      sscanf(skip_header, "%s", hostname);
      *host = '/';
      sscanf(host, "%s", path);
      *host = t;
    } else {
      sscanf(skip_header, "%s", hostname);
    }
  }
  return;
}