#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";

/* declaration for cache */
typedef struct cache_data {
  struct cache_data *prev;
  struct cache_data *next;
  size_t body_size;
  void *src;
  char uri[1024];
} cache_data;

size_t total_cache_size = 0;
cache_data *nil;
/* end of declaration */

/* declaration for thread variable arguments*/
typedef struct vargs_t {
   int connfd;
   char hostname[MAXLINE], port[MAXLINE];
} vargs_t;
/* end of declaration for thread variable arguments */

void doit(int fd);
int collect_requesthdrs(rio_t *rp, char *headers);
int parse_uri(char *uri, char *host, char *port, char *filename);
void forward_request(int clientfd, char *method, char *filename, char *host, char *port, char *headers);

cache_data *is_cached(char *uri);
void serve_fresh_response(rio_t *rp, int connfd, char *uri);
void serve_cached_response(int fd, cache_data *node);
void do_cache(void *srcp, size_t src_size, char *uri);
void push_cache_node(cache_data *node);
void delete_cache_node(cache_data *node);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *vargp);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen; struct sockaddr_storage clientaddr;
  pthread_t tid;
  vargs_t *vargs;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // init sentinel node
  nil = (cache_data *)malloc(sizeof(cache_data));
  nil->next = nil; nil->prev = nil;

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);

    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("@ Accepted connection from (%s, %s)\n", hostname, port);

    vargs = (vargs_t *)malloc(sizeof(vargs_t));
    strcpy(vargs->hostname, hostname);
    strcpy(vargs->port, port);
    vargs->connfd = connfd;
    Pthread_create(&tid, NULL, thread, (void *)vargs);
  }
  free(nil);
}
/* Tread routine */
void *thread(void *vargp) {
  vargs_t *vargs = (vargs_t *)vargp;
  char hostname[MAXLINE], port[MAXLINE];
  cache_data *node; int n;

  int connfd = vargs->connfd;
  strcpy(hostname, vargs->hostname);
  strcpy(port, vargs->port);
  Pthread_detach(pthread_self());
  Free(vargp);

  doit(connfd);

  Close(connfd);
  printf("@ Close connection to (%s, %s)\n", hostname, port);
  printf("<CACHE LIST> total_cache_size : %d\n", total_cache_size);\
  node = nil; n = 1;
  while((node = node->next) != nil) {
    printf("%d) %-40s  ->  %d bytes\n", n++, node->uri, node->body_size);
  }
  printf("\n");
  return NULL;
}

void forward_request(int clientfd, char *method, char *filename, char *host, char *port, char *headers)
{ 
  char buf[MAXLINE];
  // make request line
  sprintf(buf, "%s %s HTTP/1.0\r\n", method, filename);
  // make request headers
  sprintf(buf, "%s%s\r\n", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  // append client's headers
  if (!strstr(headers, "Host: "))
    sprintf(buf, "%sHost: %s:%s\r\n", buf, host, port);
  strcat(buf, headers);

  // send
  Rio_writen(clientfd, buf, strlen(buf));
    printf(">>>>>>>> Request headers to server\n");
    printf("%s", buf);
}

void doit(int fd) {
  int clientfd;
  char buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], filename[MAXLINE];
  rio_t rio;
  cache_data *node;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);            // 새로운 rio (connfd).
  Rio_readlineb(&rio, buf, MAXLINE);  // 첫번째 줄(request line) 읽어서 buf에 넣어줌.
  printf("<Incoming Request headers>\n");
  printf("%s", buf);
    
  sscanf(buf, "%s %s %s", method, uri, version); // buf에서 띄어쓰기로 구분된 문자열 3개를 읽어서 뒤의 변수에 넣어줌.
  if(!strlen(method) || !strlen(uri) || !strlen(version)) {
    clienterror(fd, method, "400", "Bad request",
      "Request could not be understood by the server");
    return;  
  }

  strcpy(buf, "");
  if (!collect_requesthdrs(&rio, buf)) {  // valid check, find addintional header
    clienterror(fd, method, "400", "Bad request",
              "Request could not be understood by the server");
    return;    
  }
  /* end of Read request line and headers */

  /* Make response */
  // if this request is cached:
  if ((node = is_cached(uri)) != nil) {
    printf("\n                   ██████╗ █████╗  ██████╗██╗  ██╗███████╗    ██╗  ██╗██╗████████╗    ██╗\n ░▄▌░░░░░░░░░▄    ██╔════╝██╔══██╗██╔════╝██║  ██║██╔════╝    ██║  ██║██║╚══██╔══╝    ██║\n ████████████▄    ██║     ███████║██║     ███████║█████╗      ███████║██║   ██║       ██║\n ░░░░░░░░▀▐████   ██║     ██╔══██║██║     ██╔══██║██╔══╝      ██╔══██║██║   ██║       ╚═╝\n ░░░░░░░░░░░▐██▌  ╚██████╗██║  ██║╚██████╗██║  ██║███████╗    ██║  ██║██║   ██║       ██╗\n                   ╚═════╝╚═╝  ╚═╝ ╚═════╝╚═╝  ╚═╝╚══════╝    ╚═╝  ╚═╝╚═╝   ╚═╝       ╚═╝\n\n");
    serve_cached_response(fd, node);
  }
  // not cached:
  else {
    /* make request to server */
    if (!parse_uri(uri, host, port, filename)) {
      clienterror(fd, method, "400", "Bad request",
                "Request could not be understood by the server");
      return;    
    }

    if ((clientfd = Open_clientfd(host, port)) < 0)
      return;
    forward_request(clientfd, method, filename, host, port, buf);
    /* end of request to server */

    /* redirect response to client */
    Rio_readinitb(&rio, clientfd);  // 새로운 rio (clientfd).
    serve_fresh_response(&rio, fd, uri);
    Close(clientfd);
   /* end of redirect response to client */
  }
  /* end of make response */
}

cache_data *is_cached(char *uri) {
  cache_data *node = nil->next;
  while(node != nil && strcmp(uri, node->uri)) {
    node = node->next;
  }
  return node;
}

void serve_fresh_response(rio_t *rp, int connfd, char *uri)
{                      // rio has clientfd.
  char *srcp; // source pointer
  size_t src_size = 0;
  char buf[MAXLINE];

  printf("<<<<<<<< Response headers from server\n");
  // read & make reponse line
  Rio_readlineb(rp, buf, MAXLINE);
  Rio_writen(connfd, buf, strlen(buf));
    printf("%s", buf);

  // read & make respone headers
  while(strcmp(buf, "\r\n")) {  // 0이 아닌 값(true)이 나올 때. 즉, readline했을 때 값이 "\r\n"가 아닐 때.
    Rio_readlineb(rp, buf, MAXLINE);
    if (strstr(buf, "Content-Length")){
      src_size = atoi(rindex(buf, ':')+2);
    }
    Rio_writen(connfd, buf, strlen(buf));
    printf("%s", buf);
  }

  // if body exists, read & make response body
  if (src_size) {
    srcp = malloc(src_size);
    Rio_readnb(rp, srcp, src_size);

    if (src_size <= MAX_OBJECT_SIZE) {
      do_cache(srcp, src_size, uri);
      Rio_writen(connfd, srcp, src_size);
    }
    else {
      Rio_writen(connfd, srcp, src_size);
      free(srcp);
    }
    printf("--- %d bytes of contents is sent to client. ---\n\n", src_size);
  }
}

void serve_cached_response(int fd, cache_data *node) {
  char buf[MAXLINE];
  /* response headers */
  // make request line
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  // make request headers
  sprintf(buf, "%sConnection: close\r\n", buf);
  // sprintf(buf, "%sContent-Length: %d\r\n", buf, size);
  // sprintf(buf, "%sContent-Type: %s\r\n\r\n", buf, filetype);    // use of filetype
  sprintf(buf, "%s\r\n", buf);

  // send
  Rio_writen(fd, buf, strlen(buf));
    printf("Response headers:\n");
    printf("%s", buf);

  /* response body */
  Rio_writen(fd, node->src, node->body_size);
  printf("--- %d bytes of cached contents is sent to client. ---\n\n", node->body_size);

  /* make this node fresh */
  delete_cache_node(node);
  push_cache_node(node);
}

void push_cache_node(cache_data *node) {
    // 1. 내 꺼
  node->next = nil->next;
  node->prev = nil;
  // 2. 남의 꺼
  nil->next->prev = node;
  nil->next = node;
}

void delete_cache_node(cache_data *node) {
  node->prev->next = node->next;
  node->next->prev = node->prev;
}

void do_cache(void *srcp, size_t src_size, char *uri) {
                          // rio has clientfd.
  /* make cache node */
  cache_data *node = (cache_data *)malloc(sizeof(cache_data));
  node->body_size = src_size;
  strcpy(node->uri, uri);
  node->src = srcp;

  /* insert to cache node list */
  cache_data *oldest_node;
  while (total_cache_size + node->body_size > MAX_CACHE_SIZE) {  // 캐시 공간이 충분할 때 까지 가장 오래된 노드 지운다.
    // 맨 끝 노드를 지운다.
    oldest_node = nil->prev;
    delete_cache_node(oldest_node);
    total_cache_size -= oldest_node->body_size;
    free(oldest_node->src);
    free(oldest_node);
  }
  push_cache_node(node);
  total_cache_size += src_size;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=\"ffffff\">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  /* Print the HTTP response */
  /* response headers */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  /* response body */
  Rio_writen(fd, body, strlen(body));
}

int parse_uri(char *uri, char *host, char *port, char *filename)
{
  char *host_ptr = index(uri, ':')+3;
  char *port_ptr = index(host_ptr, ':');
  char *filename_ptr = index(host_ptr, '/');
  if (!filename_ptr)
    return 0;

  if (!port_ptr){
  strncpy(host, host_ptr, filename_ptr-host_ptr);
  sprintf(port, "%s", "80");
  }
  else {
    strncpy(host, host_ptr, port_ptr-host_ptr);
    strncpy(port, port_ptr+1, filename_ptr-(port_ptr+1));
  }
  strcpy(filename, filename_ptr);
  return 1;
}

int collect_requesthdrs(rio_t *rp, char *headers)
{
  char buf[MAXLINE], name[MAXLINE], data[MAXLINE];
  char *str = "User-Agent:/Connection:/Proxy-Connection:";
  while (1) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    if (!strcmp(buf, "\r\n")) {
      sprintf(headers, "%s%s", headers, buf);
      //printf("%s", headers);
      break;
    }

    sscanf(buf, "%s%*[: ]%s", name, data);
    if(!strlen(name) || !strlen(data)) {
        return 0;
    }
    if(!strstr(str, name)) {
      sprintf(headers, "%s%s", headers, buf);
    }
    //printf("%s", headers);
  }

  return 1;
}