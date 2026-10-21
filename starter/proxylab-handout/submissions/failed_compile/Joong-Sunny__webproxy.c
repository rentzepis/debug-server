#include <stdio.h>
#include <pthread.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void *thread(void *vargp);
void doit(int connfd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *end_host, char *end_port);
void get_filetype(char *filename, char *filetype);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void make_packet(char *rqheader, char *filename, char *end_host, char *end_port);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *Connection = "Connection: close\r\n";
static const char *Proxy_Connection = "Proxy-Connection: close\r\n";
Cachelist *c_list;

int main(int argc, char **argv)
{
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /*init cachelist*/
  c_list = (Cachelist *)malloc(sizeof(Cachelist));
  c_list->head = NULL;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);

  /* run */
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp); // create threads
  }
}

void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);

  doit(connfd);

  Close(connfd);
  return NULL;
}

/* doit : 한 개의 HTTP transaction 처리 */
void doit(int connfd)
{
  int is_static;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE], end_host[MAXLINE], end_port[MAXLINE], rqheader[MAXLINE];
  int end_fd;
  size_t n;
  rio_t rio, rio2;

  /* Request Line과 헤더를 읽기 */
  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);

  // only for HEAD, GET methods
  if (!strcasecmp(method, "HEAD"))
    parse_uri(uri, filename, end_host, end_port);
  else if (!strcasecmp(method, "GET"))
    parse_uri(uri, filename, end_host, end_port);
  else
  {
    clienterror(connfd, method, "501", "Not implemented", "Tiny does not implement this method");
    return;
  }

  CachedData *hitted_cache = findcache(c_list, filename);

  if (hitted_cache != NULL)
  { // data <IS IN> cachelist
    Rio_writen(connfd, hitted_cache->c_val, MAX_OBJECT_SIZE);
  }
  else
  { // data <IS NOT IN> cachelist
    make_packet(rqheader, filename, end_host, end_port);
    end_fd = Open_clientfd(end_host, end_port);

    if (end_fd < 0)
    {
      printf("Error: file cannot find proper end_host, end_port!!");
      return;
    }

    // init cache
    CachedData *newcache = (CachedData *)malloc(sizeof(CachedData));
    Rio_writen(end_fd, rqheader, strlen(rqheader));
    Rio_readinitb(&rio2, end_fd);
    Rio_readnb(&rio2, buf, MAX_OBJECT_SIZE);
    // cache <- (data)
    strcpy(newcache->c_val, buf);
    strcpy(newcache->c_key, filename);
    insertcache(c_list, newcache);
    // (data)-> client
    Rio_writen(connfd, buf, MAX_OBJECT_SIZE);
  }
}

void make_packet(char *rqheader, char *filename, char *end_host, char *end_port)
{ /* Assemble packet with components*/
  char filetype[MAXLINE];
  get_filetype(filename, filetype);
  sprintf(rqheader, "GET %s HTTP/1.0\r\n", filename);
  sprintf(rqheader, "%sHOST: %s\r\n", rqheader, end_host);
  sprintf(rqheader, "%s%s", rqheader, Proxy_Connection);
  sprintf(rqheader, "%s%s", rqheader, user_agent_hdr);
  sprintf(rqheader, "%s%s", rqheader, Connection);
  sprintf(rqheader, "%sContent-type: %s\r\n\r\n", rqheader, filetype);
}

/* parse_uri: fill <filename, end_host, end_port> with uri */
int parse_uri(char *uri, char *filename, char *end_host, char *end_port)
{
  char *p, *temp, *temp2;
  char temp_uri[300];
  char *buf;
  buf = uri;
  p = strchr(buf, '/');
  strcpy(temp_uri, p + 2);
  p = strchr(temp_uri, ':');

  if (p == '\0')
  { // if port_number does not exist in uri
    p = strchr(temp_uri, '/');
    strcpy(end_host, p);
    *p = '\0';
    strcpy(filename, temp_uri);
    strcpy(end_port, "80");
  }
  else
  { // port_number exist in uri
    *p = '\0';
    temp = strchr(p + 1, '/');
    strcpy(filename, temp);
    *temp = '\0';
    strcpy(end_port, p + 1);
    strcpy(end_host, temp_uri);
  }
  return 1;
}

void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}