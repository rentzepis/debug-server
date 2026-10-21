#include <stdio.h>
#include "csapp.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define BUF_SIZE 500

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int connfd);
int call_end_server(char *hostname, int port);
void uri_handler(char *hostname, char *path, int *port, char *uri);
void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int main(int argc, char *argv[])
{
  int sfd, *connfd;
  struct sockaddr_storage peer_addr;
  socklen_t peer_addr_len;
  char hostname[MAXLINE], port[MAXLINE];
  pthread_t tid;

  if (argc != 2)
  {
    fprintf(stderr, "Usage: %s port\n", argv[0]);
    exit(1);
  }

  struct sockaddr_in ip4addr;
  memset(&ip4addr, 0, sizeof(struct sockaddr_in));
  ip4addr.sin_family = AF_INET;
  ip4addr.sin_port = htons(atoi(argv[1]));
  ip4addr.sin_addr.s_addr = INADDR_ANY;

  if ((sfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    perror("socket error");
    exit(EXIT_FAILURE);
  }
  if (bind(sfd, (struct sockaddr *)&ip4addr, sizeof(struct sockaddr_in)) < 0)
  {
    close(sfd);
    perror("bind error");
    exit(EXIT_FAILURE);
  }
  if (listen(sfd, 100) < 0)
  {
    close(sfd);
    perror("listen error");
    exit(EXIT_FAILURE);
  }

  while (1)
  {
    peer_addr_len = sizeof(peer_addr);
    connfd = Malloc(sizeof(int));
    *connfd = Accept(sfd, (SA *)&peer_addr, &peer_addr_len);
    Getnameinfo((SA *)&peer_addr, peer_addr_len, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    // doit(connfd);
    pthread_create(&tid, NULL, thread, connfd);
  }

  return 0;
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int connfd)
{

  int six_four = 65536;
  char req_buff[six_four];

  int bytes_read = recv(connfd, req_buff, six_four - 1, 0); // READ
  printf("Received %d bytes: %s\n", bytes_read, req_buff);

  char method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE], path[MAXLINE], endserver_http_header[MAXLINE];
  char request_header[MAXLINE];
  char host_line[MAXLINE];
  int port;

  sscanf(req_buff, "%s %s %s", method, uri, version);

  printf("method: %s\n", method);
  printf("uri: %s\n", uri);
  printf("version: %s\n", version);

  uri_handler(hostname, path, &port, uri);

  printf("\nhostname: %s\n", hostname);
  printf("path: %s\n", path);
  printf("port: %d\n", port);

  sprintf(request_header, "GET %s HTTP/1.0\r\n", path);
  sprintf(host_line, "Host: %s:%d\r\n", hostname, port);

  sprintf(endserver_http_header, "%s%s%s%s%s%s",
          request_header, host_line,
          "Connection: close\r\n",
          "Proxy-Connection: close\r\n",
          user_agent_hdr,
          "\r\n");

  // printf(request_header);
  // printf(host_line);
  // printf(endserver_http_header);

  ///    connect to end server

  int end_server_file_descriptor = call_end_server(hostname, port);

  printf("cfd: %d\n", end_server_file_descriptor);

  size_t nleft = strlen(endserver_http_header);
  ssize_t nwritten;
  char *bufp = endserver_http_header;

  // check_cache_here

  while (nleft > 0) // write to end server
  {
    if ((nwritten = write(end_server_file_descriptor, endserver_http_header, strlen(endserver_http_header))) <= 0)
    {
      if (errno == EINTR) /* Interrupted by sig handler return */
        nwritten = 0;     /* and call write() again */
    }
    nleft -= nwritten;
    bufp += nwritten;
  }

  char end_server_buffer[MAXLINE];

  int end_server_bytes_read;

  while ((end_server_bytes_read = recv(end_server_file_descriptor, end_server_buffer, MAXLINE, 0)) > 0)
  {
    size_t xleft = end_server_bytes_read;
    ssize_t xwritten;
    char *xbufp = end_server_buffer;

    while (xleft > 0)
    {
      if ((xwritten = write(connfd, xbufp, xleft)) <= 0)
      {
        if (errno == EINTR) /* Interrupted by sig handler return */
          xwritten = 0;     /* and call write() again */
      }
      xleft -= xwritten;
      xbufp += xwritten;
    }
  }

  Close(end_server_file_descriptor);
}

void uri_handler(char *hostname, char *path, int *port, char *uri)
{
  // default port should be set to 80
  *port = 80;
  char *index = strstr(uri, "//"); // plain url?

  if (index == NULL)
  {
    index = uri;
  }
  else
  {
    index = index + 2;
  }

  // http://localhost:15214
  // www.example.com

  char *offset = strstr(index, ":");

  if (offset == NULL) // negate default port
  {
    offset = strstr(index, "/");
    if (offset == NULL)
    {
      sscanf(index, "%s", hostname);
    }
    else
    {
      *offset = '\0';
      sscanf(index, "%s", hostname);
      *offset = '/';
      sscanf(offset, "%s", path);
    }
  }
  else
  {
    *offset = '\0';
    sscanf(index, "%s", hostname);
    sscanf(offset + 1, "%d%s", port, path);
  }
  return;
}

int call_end_server(char *hostname, int portyport)
{

  char port[100];
  sprintf(port, "%d", portyport);

  int clientfd, rc;
  struct addrinfo hints, *listp, *p;

  /* Get a list of potential server addresses */
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_UNSPEC;     /* Allow IPv4 or IPv6 */
  hints.ai_socktype = SOCK_STREAM; /* Open a connection */
  hints.ai_flags = 0;              /* For wildcard IP address */
  hints.ai_protocol = 0;           /* Any protocol */

  if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0)
  {
    fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, port, gai_strerror(rc));
    return -2;
  }

  /* Walk the list for one that we can successfully connect to */
  for (p = listp; p; p = p->ai_next)
  {
    /* Create a socket descriptor */
    if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0)
      continue; /* Socket failed, try the next */

    /* Connect to the server */
    if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1)
      break; /* Success */
    if (close(clientfd) < 0)
    { /* Connect failed, try another */ //line:netp:openclientfd:closefd
      fprintf(stderr, "open_clientfd: close failed: %s\n", strerror(errno));
      return -1;
    }
  }

  /* Clean up */
  freeaddrinfo(listp);
  if (!p) /* All connects failed */
    return -1;
  else /* The last connect succeeded */
    return clientfd;
}