#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define HTTP_PORT 80

void *handle_thread(int *socket_fd_pointer);
void handle_client_request(int clientfd);
void parse(const char *uri, char *hostname, char *path, char *port);
void build_request_header(rio_t *rio, char *header, char *path);

/* Global Variables */
cache* cache_list;

int main(int argc, char **argv)
{
  int listen_fd;
  int *conn_fd_p;
  struct sockaddr_storage clientaddr;
  char hostname[MAXLINE], port[MAXLINE];
  int clientlen = sizeof(clientaddr);
  pthread_t tid;

  // Check command line arguments
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // Listen for connections
  listen_fd = Open_listenfd(argv[1]);

  // Initiate cache list
  cache_list = init_cache();

  // Execute server loop
  while (1)
  {
    // Allocate a memory for connection descriptor
    conn_fd_p = Malloc(sizeof(int));
    if (conn_fd_p == NULL)
    {
      fprintf(stderr, "Memory allocation failed.\n");
      exit(1);
    }

    // Accept a connection
    *conn_fd_p = Accept(listen_fd, (SA *)&clientaddr, (socklen_t *)&clientlen);

    // Create a thread
    Pthread_create(&tid, NULL, handle_thread, conn_fd_p);
  }

  return 0;
}

/*
Handle each client request in a separate thread
- This function is called for each new connection.
- After processing it frees allocated resources and terminates thread.
*/
void *handle_thread(int *socket_fd_pointer)
{
  int conn_fd = *((int *)socket_fd_pointer);
  Pthread_detach(pthread_self());
  Free(socket_fd_pointer);
  handle_client_request(conn_fd);
  Close(conn_fd);
  return NULL;
}

/* Handle the request of client */
void handle_client_request(int clientfd)
{
  rio_t rio_client, rio_server; // Robust I/O package
  char request_buf[MAXLINE], header_buf[MAXLINE], response_buf[MAXLINE], cache_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  int proxyfd;

  Rio_readinitb(&rio_client, clientfd);
  if (Rio_readlineb(&rio_client, request_buf, MAXLINE) <= 0)
  {
    printf("Invalid Request.\n");
    return;
  }
  sscanf(request_buf, "%s %s %s", method, uri, version);

  // If cached data exists, return the cached data
  node *cached_node = find_cache(cache_list, uri);
  if (cached_node != NULL) 
  {
    Rio_writen(clientfd, cached_node->data, cached_node->size);
    return;
  }

  parse(uri, hostname, path, port);

  build_request_header(&rio_client, header_buf, path);

  // Connect to the remote server and forward request from client
  proxyfd = Open_clientfd(hostname, port);
  Rio_readinitb(&rio_server, proxyfd);
  Rio_writen(proxyfd, header_buf, strlen(header_buf));

  // Forward the response from remote server back to the client
  size_t n;
  while ((n = Rio_readnb(&rio_server, response_buf, MAXLINE)) > 0)
  {
    sprintf(cache_buf, "%s%s", cache_buf, response_buf);
    Rio_writen(clientfd, response_buf, n);
  }

  // Store the server's response in the cache
  add_cache(cache_list, uri, cache_buf, sizeof(cache_buf));
}

/* Parse URI to extract host name, path, and port */
void parse(const char *uri, char *hostname, char *path, char *port)
{
  const char *protocol = "http://";
  const int protocol_len = strlen(protocol);

  const char *host_start = uri + protocol_len;
  const char *host_end = strchr(host_start, '/');

  // hostname & path
  if (host_end == NULL)
  {
    strcpy(hostname, host_start);
    strcpy(path, "/");
  }
  else
  {
    strncpy(hostname, host_start, host_end - host_start);
    hostname[host_end - host_start] = '\0';
    strcpy(path, host_end);
  }

  // port
  char *colon = strchr(hostname, ':');
  if (colon != NULL)
  {
    sscanf(colon + 1, "%[^/]", port);
    *colon = '\0';
  }
  else
  {
    strcpy(port, HTTP_PORT);
  }
}

/* Build HTTP request header based on the client's request */
void build_request_header(rio_t *rio, char *header, char *path)
{
  char buf[MAXLINE];
  // Modify http version from 1.1 to 1.0
  sprintf(header, "GET %s HTTP/1.0\r\n", path);
  // Copy rest of header except above one
  while (Rio_readlineb(rio, buf, MAXLINE) > 2)
  {
    if (strncasecmp(buf, "GET", 3) != 0)
    {
      sprintf(header, "%s%s", header, buf);
    }
  }
  sprintf(header, "%s\r\n", header);
}