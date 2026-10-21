#include "csapp.h"
#include "sbuf.h"
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define SBUFSIZE 16
#define NTHREADS 4

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

static sbuf_t sbuf;
static int readcnt;
static sem_t mutex, w;

typedef struct {
  char *name;
  char *object;
  int used_cnt;
} Cache;

void doit(int fd);
void parse_uri(char *uri, char *hostname, char *port, char *filename);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void transfer_hdrs(int clientfd, rio_t *rio, char *hostname);
void *thread(void *vargp);

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int listenfd, connfd, i;
  char hostname[MAXLINE], port[MAXLINE];
  pthread_t tid;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  listenfd = Open_listenfd(argv[1]);

  sbuf_init(&sbuf, SBUFSIZE);
  for (i = 0; i < NTHREADS; i++) {
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
  int clientfd;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE],
      hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio, clientrio;
  size_t n;

  Rio_readinitb(&rio, fd);
  if (!Rio_readlineb(&rio, buf, MAXLINE))
    return;
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
    return;
  }
  parse_uri(uri, hostname, port, path);
  printf("hostname: %s\n", hostname);
  printf("port: %s\n", port);
  printf("path: %s\n", path);
  clientfd = Open_clientfd(hostname, port);
  Rio_readinitb(&clientrio, clientfd);
  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  printf("%s", buf);
  Rio_writen(clientfd, buf, strlen(buf));
  printf("before transfer\n");
  transfer_hdrs(clientfd, &rio, hostname);
  while ((n = Rio_readlineb(&clientrio, buf, MAXLINE)) != 0) {
    Rio_writen(fd, buf, n);
    printf("transfer %zu bytes\n", n);
  }
  Close(clientfd);
}

void *thread(void *vargp) {
  Pthread_detach(pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);
    Close(connfd);
  }
}

void parse_uri(char *uri, char *hostname, char *port, char *path) {
  char *hostptr, *portptr, *pathptr;
  hostptr = strstr(uri, "//");
  if (hostptr) {
    hostptr += 2;
    portptr = index(hostptr, ':');
    pathptr = index(hostptr, '/');
    if (portptr) {
      strncpy(hostname, hostptr, portptr - hostptr);
      if (pathptr) {
        strcpy(path, pathptr);
        strncpy(port, portptr + 1, pathptr - portptr - 1);
      } else {
        strcpy(port, portptr + 1);
        strcpy(path, "/");
      }
    } else {
      strcpy(port, "80");
      if (pathptr) {
        strcpy(path, pathptr);
        strncpy(hostname, hostptr, pathptr - hostptr);
      } else {
        strcpy(hostname, hostptr);
        strcpy(path, "/");
      }
    }
  } else {
    strcpy(hostname, "");
    portptr = index(uri, ':');
    pathptr = index(uri, '/');
    strcpy(port, "80");
    if (pathptr) {
      strcpy(path, pathptr);
    } else {
      strcpy(path, "/");
    }
  }
}

void transfer_hdrs(int clientfd, rio_t *rio, char *hostname) {
  printf("in transfers\n");
  char buf[MAXLINE];
  int include_host = 0;

  Rio_readlineb(rio, buf, MAXLINE);
  printf("Income header: %s", buf);
  while (strcmp(buf, "\r\n")) {
    if (strncmp(buf, "User-Agent", 10) && strncmp(buf, "Connection", 10) &&
        strncmp(buf, "Proxy-Connection", 16)) {
      if (!strncmp(buf, "Host", 4)) {
        include_host = 1;
      }
      printf("%s", buf);
      Rio_writen(clientfd, buf, strlen(buf));
    }
    Rio_readlineb(rio, buf, MAXLINE);
  }
  sprintf(buf, "Connection: close\r\n");
  printf("%s", buf);
  Rio_writen(clientfd, buf, strlen(buf));
  sprintf(buf, "Proxy-Connection: close\r\n");
  printf("%s", buf);
  Rio_writen(clientfd, buf, strlen(buf));
  sprintf(buf, "%s", user_agent_hdr);
  printf("%s", buf);
  Rio_writen(clientfd, buf, strlen(buf));
  if (!include_host) {
    sprintf(buf, "Host: %s\r\n", hostname);
    printf("%s", buf);
    Rio_writen(clientfd, buf, strlen(buf));
  }
  sprintf(buf, "\r\n");
  printf("%s", buf);
  Rio_writen(clientfd, buf, strlen(buf));
}

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
  sprintf(buf, "<body bgcolor="
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

int reader(int fd, char *url) {
  while (1) {
    int from_cache =
        0; /* equals 0 => obj not from cache; equals 1 => obj from cache */

    P(&mutex);
    readcnt++;
    if (readcnt == 1) /* First in */
      P(&w);
    V(&mutex);

    /* obj from cache then we should write content to fd of real client */
    for (int i = 0; i < NUMBERS_OBJECT; i++) {
      if (cache[i].is_used && (strcmp(url, cache[i].url) == 0)) {
        from_cache = 1;
        Rio_writen(fd, cache[i].content, MAX_OBJECT_SIZE);
        *(cache[i].cnt)++;
        break;
      }
    }

    P(&mutex);
    readcnt--;
    if (readcnt == 0) /* last out */
      V(&w);
    V(&mutex);

    return from_cache;
  }
}