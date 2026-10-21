#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

int sendProxy(char *client_hostname, char *client_port, int connfd);
void parseUrl(char *uri, char *host, char *port, char *path);
int serveStatic(int fd, char *filename);
void init();
int pathToFileName(char *uri, char *filename);
void sigchild_handler(int sig);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
sem_t s;

int main(int argc, char **argv)
{
  int listenfd, connfd, proxyfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  char client_hostname[MAXLINE], client_port[MAXLINE];
  init();

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(struct sockaddr_storage);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    printf("Connected to (%s, %s)\n", client_hostname, client_port);
    if (Fork() == 0) /* Child Process */
    {
      sendProxy(client_hostname, client_port, connfd);
      // Close(connfd);
    }
    Close(connfd); /* Parent Process Clean up */
  }
  exit(0);
}

void init()
{
  Signal(SIGPIPE, SIG_IGN);
  Signal(SIGCHLD, sigchild_handler);
}

int sendProxy(char *client_hostname, char *client_port, int connfd)
{
  int clientfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], path[MAXLINE], filename[MAXLINE];
  rio_t rio;
  struct stat sbuf;

  Rio_readinitb(&rio, connfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("\n============ Check Proxy ============\n");
  printf("Request headers: %s \n", buf);

  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcasecmp(method, "GET"))
  {
    printf("Proxy does not implement this method\n");
    return -1;
  }
  parseUrl(uri, host, port, path);
  pathToFileName(path, filename);

  if (!strstr(uri, "cgi-bin"))
  {
    if (serveStatic(connfd, filename) > 0)
    {
      return -1;
    }
  }

  clientfd = open_clientfd(host, port);
  if (clientfd < 0)
  {
    printf("Proxy could not connect to server\nTry to use cache\n");
    return -1;
  }

  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%s%s", buf, user_agent_hdr);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  sprintf(buf, "%s\r\n", buf);
  Rio_writen(clientfd, buf, strlen(buf));

  printf("\n============ Delivery Proxy ============\n");
  char client_buf[MAXLINE];
  rio_t client_rio;
  Rio_readinitb(&client_rio, clientfd);
  size_t n;
  int file_size = 0;
  int is_body_start = 0;

  sem_wait(&s);
  int cache_file_fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);

  while ((n = Rio_readlineb(&client_rio, client_buf, MAXLINE)) != 0)
  {
    Rio_writen(connfd, client_buf, n);
    printf("%s", client_buf);
    if (is_body_start)
    {
      Rio_writen(cache_file_fd, client_buf, n);
      file_size += n;
    }
    if (client_buf[0] == '\r')
      is_body_start = 1;
  }

  Close(cache_file_fd);

  if (file_size > MAX_OBJECT_SIZE)
  {
    remove(filename);
  }
  sem_post(&s);
  Close(clientfd);

  return 0;
}

void parseUrl(char *uri, char *host, char *port, char *path)
{
  char *ptr = strstr(uri, "//");
  if (ptr == NULL)
  {
    if (uri != NULL)
      ptr = uri;
  }
  else
  {
    ptr += 2;
  }

  char *ptr2 = strstr(ptr, ":");
  if (ptr2 == NULL)
  {
    ptr2 = strstr(ptr, "/");
    if (port != NULL)
      strcpy(port, "80");
  }
  else
  {
    *ptr2 = '\0';
    if (port != NULL)
      strcpy(port, ptr2 + 1);
    if (port != NULL)
      ptr2 = strstr(port, "/");
  }

  if (ptr2 == NULL)
  {
    if (path != NULL)
      strcpy(path, "/");
  }
  else
  {
    if (path != NULL)
      strcpy(path, ptr2);
    *ptr2 = '\0';
  }
  if (host != NULL)
    strcpy(host, ptr);
}

int serveStatic(int fd, char *filename)
{
  int srcfd, filesize;
  struct stat sbuf;
  char *srcp, filetype[MAXLINE], buf[MAXBUF], paths[MAXLINE];

  printf("\n============ Serve Static File ============\n");
  printf("filename: %s\n", filename);

  if (stat(filename, &sbuf) < 0)
  {
    printf("ServeStatic error: file not exist\n");
    return -1;
  }

  filesize = sbuf.st_size;
  if (filesize <= 0)
  {
    return -1;
  }

  /* Send response headers to client */
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Proxy Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  /* Send response body to client */
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
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
  else if (strstr(filename, ".mpg"))
    strcpy(filetype, "video/mpeg");
  else if (strstr(filename, ".mp4"))
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

int pathToFileName(char *path, char *filename)
{
  strcpy(filename, "./.tmp_proxy");
  strcat(filename, path);
  if (path[strlen(path) - 1] == '/')
    strcat(filename, "home.html");
  return 1;
}

void sigchild_handler(int sig)
{
  while (waitpid(-1, 0, WNOHANG) > 0)
    ;
  return;
}