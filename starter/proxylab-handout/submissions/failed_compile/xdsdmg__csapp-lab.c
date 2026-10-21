#include "cache.h"
#include "csapp.h"
#include "http.h"
#include "sbuf.h"
#include <stdio.h>
#include <stdlib.h>

static sbuf_t *line;
static Cache *cache;

typedef struct sockaddr_storage sockaddr_storage;

void doit(int conn_fd);
void print_uri(URI *uri_data);
void *thread(void *vargp);

/* handle sigpipe signal */
void sigpipe_handler(int sig) {
  printf("sigpipe handled %d\n", sig);
  return;
}

int main(int argc, char **argv) {
  Signal(SIGPIPE, sigpipe_handler);

  char hostname[MAXLINE], port[MAXLINE];

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int listen_fd = Open_listenfd(argv[1]);

  printf("Proxy is listening at port: %s\n", argv[1]);

  line = (sbuf_t *)malloc(sizeof(sbuf_t));
  sbuf_init(line, SBUFSIZE);
  cache = (Cache *)malloc(sizeof(Cache));
  cache_init(cache);

  pthread_t tid;
  for (int i = 0; i < NTHREADS; i++) {
    Pthread_create(&tid, NULL, thread, NULL);
  }

  while (1) {
    sockaddr_storage client_addr;
    socklen_t client_len = sizeof(sockaddr_storage);

    int conn_fd = Accept(listen_fd, (SA *)&client_addr, &client_len);

    Getnameinfo((SA *)&client_addr, client_len, hostname, MAXLINE, port,
                MAXLINE, 0);

    printf("Accepted connection from (%s %s).\n", hostname, port);

    sbuf_insert(line, conn_fd);
  }

  return 0;
}

void doit(int conn_fd) {
  Request *req = (Request *)malloc(sizeof(Request));
  if (parse_request(conn_fd, req) < 0)
    return;

  char req_payload[MAX_PAYLOAD_SIZE];
  build_request_payload(req, req_payload);

  int in_cache = reader(cache, conn_fd, req->request_line);
  if (in_cache == 1)
    return;

  int server_fd = Open_clientfd(req->uri->host, req->uri->port);
  if (server_fd < 0) {
    printf("Connection failed\n");
    return;
  }

  rio_t server_rio;
  Rio_readinitb(&server_rio, server_fd);
  Rio_writen(server_fd, req_payload, strlen(req_payload));

  uint8_t resp_payload[MAX_PAYLOAD_SIZE];
  Response *resp = (Response *)malloc(sizeof(Response));
  parse_response(server_fd, &server_rio, resp);
  size_t resp_payload_size = build_response_payload(resp, resp_payload);
  Rio_writen(conn_fd, resp_payload, resp_payload_size);

  if (resp_payload_size <= MAX_OBJECT_SIZE)
    writer(cache, req->request_line, resp_payload, resp_payload_size);

  Close(server_fd);
}

void *thread(void *vargp) {
  Pthread_detach(Pthread_self());
  while (1) {
    int conn_fd = sbuf_remove(line);
    doit(conn_fd);
    Close(conn_fd);
  }
}
