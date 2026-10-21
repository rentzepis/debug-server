#include "cache.h"
#include "csapp.h"
#include "tiny_c_log/log_posix.h"
#define MAX(a, b) (a) < (b) ? (b) : (a)
#define MIN(a, b) (a) < (b) ? (a) : (b)

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *prox_hdr = "Proxy-Connection: close\r\n";

int listenfd;

/*
 * The URL received by proxy formed like http://www.cmu.edu/hub/index.html
 * parse_uri split the target host, port, and path
 */
int parse_uri(const char *uri, const char **host, const char **port,
              const char **path) {
  *host = strstr(uri, "//");
  if (*host) {
    *host += 2;
  } else {
    *host = uri;
  }
  // If contains port, it forms like http:://www.cmu.edu:8080/hub/index.html
  *port = index(*host, ':');
  if (*port) {
    *path = ++(*port);
    while (isdigit(**path)) (*path)++;
  } else {
    *path = index(*host, '/');
  }
  LOG_DEBUG("path is %s", *path);
  return 0;
}

/*
 * build_requesthdrs - build a new HTTP request headers for the proxy target
 */
char *build_requesthdrs(rio_t *rp, char *dst, const char *host,
                        const char *port) {
  if (port) {
    dst += sprintf(dst, "Host: %s:%s\r\n", host, port);
  } else {
    dst += sprintf(dst, "Host: %s\r\n", host);
  }
  dst += sprintf(dst, "%s%s%s", user_agent_hdr, conn_hdr, prox_hdr);

  char buf[MAXLINE];
  while (Rio_readlineb(rp, buf, MAXLINE)) {
    LOG_DEBUG("[header] %s", buf);
    if (strstr(buf, "Host:") || strstr(buf, "User-Agent:") ||
        strstr(buf, "Connection:") || strstr(buf, "Proxy-Connection:")) {
      continue;
    }
    dst += sprintf(dst, "%s", buf);
    if (strcmp(buf, "\r\n") == 0) break;
  }

  return dst;
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE];

  /* Print the HTTP response headers */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* Print the HTTP response body */
  sprintf(buf, "<html><title>Tiny Error</title>");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf,
          "<body bgcolor="
          "ffffff"
          ">\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
  Rio_writen(fd, buf, strlen(buf));
}

/*
 * doit - handle one HTTP request/response transaction
 */
void doit(int client_fd) {
  char buf[MAX_OBJECT_SIZE + MAXLINE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  rio_t rio_client, rio_server;

  /* Read request line and headers */
  Rio_readinitb(&rio_client, client_fd);
  if (!Rio_readlineb(&rio_client, buf, MAXLINE)) {
    return;
  }
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    clienterror(client_fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }

  char *cache;
  size_t len;
  find_cache(uri, &cache, &len);
  if (cache) {
    Rio_writen(client_fd, cache, len);
    close(client_fd);
    return;
  }

  const char *host, *port, *path;
  parse_uri(uri, &host, &port, &path);
  LOG_DEBUG("[ptr] host:%p port:%p path:%p [offset] host:%d port:%d path:%d",
            host, port, path, host - uri, port - uri, path - uri);
  char host_buf[MAXLINE], port_buf[10];
  if (port) {
    memcpy(host_buf, host, port - host - 1);
    host_buf[port - host - 1] = '\0';
    memcpy(port_buf, port, MIN(9, path - port));
    port_buf[MIN(9, path - port)] = '\0';
  } else {
    memcpy(host_buf, host, path - host);
    host_buf[path - host] = '\0';
  }

  /* Open a new connection to the target */
  LOG_DEBUG("hostname: %s ; port: %s", host_buf, port ? port_buf : "");
  int server_fd = Open_clientfd(host_buf, port ? port_buf : "80");
  LOG_DEBUG("server_fd = %d", server_fd);
  Rio_readinitb(&rio_server, server_fd);
  // fill up the request line, it forms like GET /hub/index.html HTTP/1.0
  int n = sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  char *end =
      build_requesthdrs(&rio_client, buf + n, host_buf, port ? port_buf : NULL);
  Rio_writen(server_fd, buf, end - buf);  // send client header to real server
  len = 0;
  int offset = 0;
  while ((n = Rio_readlineb(&rio_server, buf + offset, MAXLINE))) {
    Rio_writen(client_fd, buf + offset, n);
    offset += n;
    len += n;
    if (offset > MAX_OBJECT_SIZE) offset = 0;
  }
  if (len <= MAX_OBJECT_SIZE) {
    insert_object(uri, buf, len);
  }
  close(server_fd);
  close(client_fd);
}

void *doint_wrap(void *connfd) {
  doit(*(int *)connfd);
  free(connfd);
  return NULL;
}

void doinnewthread(int connfd) {
  pthread_t tid;
  int *fd = malloc(sizeof(int));
  *fd = connfd;
  Pthread_create(&tid, NULL, doint_wrap, fd);
  Pthread_detach(tid);
}

void proxyexit(int sig) {
  free_cache();
  close(listenfd);
  printf("proxy exit. sig %d\n", sig);
  exit(0);
}

int main(int argc, char **argv) {
  int connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  signal(SIGPIPE, SIG_IGN);
  signal(SIGABRT, proxyexit);
  signal(SIGTERM, proxyexit);
  signal(SIGINT, proxyexit);

  init_cache(MAX_CACHE_SIZE, MAX_OBJECT_SIZE);

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doinnewthread(connfd);
  }
  return 0;
}
