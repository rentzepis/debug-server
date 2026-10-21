#include "cache.h"
#include "csapp.h"
#include "sbuf.h"
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/wait.h>
#define NTHREADS 4
#define SBUFSIZE 16

/* Recommended max cache and object sizes */
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
struct socketinfo {
  char hostname[MAXLINE];
  char port[MAXLINE];
  char uri[MAXLINE];
};

struct socketinfo parse_url(char *url);
void requset_header_parser(rio_t *rp);
void doit(int fd);
sbuf_t sbuf;
void *thread(void *vargp);

CacheBlock cache;
int main(int argc, char **argv) {
  int listenfd, browser;
  pthread_t tid;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  Cacheinit(&cache);
  Signal(SIGPIPE, SIG_IGN);
  if (argc != 2) {
    fprintf(stdout, "usage: ./%s <port>\n", *argv);
    exit(1);
  }
  listenfd = Open_listenfd(argv[1]);
  sbuf_init(&sbuf, SBUFSIZE);
  for (int i = 0; i < NTHREADS; ++i) {
    pthread_create(&tid, NULL, thread, NULL);
  }
  while (1) {
    clientlen = sizeof(clientaddr);
    browser = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                NI_NUMERICSERV);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    sbuf_insert(&sbuf, browser);
  }
}

void doit(int fd) {
  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE],
      errorbuf[MAXLINE], requestToserver[MAXBUF], proxy_buf[MAXLINE], *cachebuf;
  rio_t rio_client, rio_server;
  int proxyfd, num, byte_cnt;
  struct socketinfo serverinfo;
  Rio_readinitb(&rio_client, fd);
  cachebuf = Malloc(MAX_OBJECT_SIZE);
  if (rio_readlineb(&rio_client, buf, MAXLINE) < 0) {
    if (errno == ECONNRESET) {
      fprintf(stderr, "Connection reset by peer . Closing socket.\n");
      Free(cachebuf);
      return;
    } else {
      fprintf(stderr, "another error. Closing socket.\n");
      Free(cachebuf);
      return;
    }
  }
  printf("%s\n", buf);
  if (!strcmp(buf, "\r\n")) {
    fprintf(stderr, "no request\n");
    Free(cachebuf);
    return;
  }
  sscanf(buf, "%s %s %s", method, url, version);
  if (strcasecmp(method, "GET")) {
    sprintf(errorbuf, "the usage: GET url version\n");
    if (rio_writen(fd, errorbuf, strlen(errorbuf)) < 0) {

      if (errno == EPIPE) {
        perror("dyh");
        Free(cachebuf);
        return;
      } else {
        perror("another error");
        Free(cachebuf);
        return;
      }
    }
    return;
  }
  if ((byte_cnt = findCache(&cache, url, cachebuf)) > 0) {
    Rio_writen(fd, cachebuf, byte_cnt);
    printf("use cache data\n");
    Free(cachebuf);
    return;
  }
  version[7] = '0';            // HTTP/1.1 -> HTTP/1.0
  serverinfo = parse_url(url); // extract the property of the url
  // printf("request server:\nhostname :%s port :%s uri :%s\n",
  //        serverinfo.hostname, serverinfo.port, serverinfo.uri);
  // connect to server
  proxyfd = Open_clientfd(serverinfo.hostname, serverinfo.port);
  Rio_readinitb(&rio_server, proxyfd);
  sprintf(requestToserver, "GET %s %s\r\n", serverinfo.uri, version);
  sprintf(requestToserver + strlen(requestToserver), "Host: %s\r\n",
          serverinfo.hostname);
  sprintf(requestToserver + strlen(requestToserver), "%s", user_agent_hdr);
  sprintf(requestToserver + strlen(requestToserver), "Connection: close\r\n");
  sprintf(requestToserver + strlen(requestToserver),
          "Proxy-Connection: close\r\n");
  // concatenate client's request header and proxy's header

  if (rio_readlineb(&rio_client, buf, MAXLINE) < 0) {
    if (errno == ECONNRESET) {
      fprintf(stderr, "Connection reset by peer . Closing socket.\n");
      Close(proxyfd);
      Free(cachebuf);
      return;
    } else {
      fprintf(stderr, "another error. Closing socket.\n");
      Close(proxyfd);
      Free(cachebuf);
      return;
    }
  }
  while (strcmp(buf, "\r\n")) {
    if (strstr(buf, "Host") || strstr(buf, "User-Agent") ||
        strstr(buf, "Connection")) {
      if (rio_readlineb(&rio_client, buf, MAXLINE) < 0) {
        if (errno == ECONNRESET) {
          fprintf(stderr, "Connection reset by peer . Closing socket.\n");
          Close(proxyfd);
          Free(cachebuf);
          return;
        } else {
          fprintf(stderr, "another error. Closing socket.\n");
          Close(proxyfd);
          Free(cachebuf);
          return;
        }
      }
    } else {
      strcat(requestToserver, buf);
      if (rio_readlineb(&rio_client, buf, MAXLINE) < 0) {
        if (errno == ECONNRESET) {
          fprintf(stderr, "Connection reset by peer . Closing socket.\n");
          Close(proxyfd);
          Free(cachebuf);
          return;
        } else {
          fprintf(stderr, "another error. Closing socket.\n");
          Close(proxyfd);
          Free(cachebuf);
          return;
        }
      }
    }
  }
  strcat(requestToserver, "\r\n");

  // end concatenate
  printf("%s\n", requestToserver);
  if (rio_writen(proxyfd, requestToserver, strlen(requestToserver)) < 0) {

    if (errno == EPIPE) {
      perror("error pipe");
      Close(proxyfd);
      Free(cachebuf);
      return;
    } else {
      perror("another");
      Close(proxyfd);
      Free(cachebuf);
      return;
    }
  }
  byte_cnt = 0;
  char *tembuf = cachebuf;
  while ((num = rio_readnb(&rio_server, proxy_buf, MAXLINE))) {
    if (num < 0) {
      if (errno == ECONNRESET) {
        fprintf(stderr, "Connection reset by peer . Closing socket.\n");
        Close(proxyfd);
        Free(cachebuf);
        return;
      } else {
        fprintf(stderr, "another error. Closing socket.\n");
        Close(proxyfd);
        Free(cachebuf);
        return;
      }
    }
    if (num == 0) {
      break;
    }
    byte_cnt += num;
    if (byte_cnt <= MAX_OBJECT_SIZE) {
      memcpy(tembuf, proxy_buf, num);
      tembuf += num;
    }
    if (rio_writen(fd, proxy_buf, num) < 0) {

      if (errno == EPIPE) {
        fprintf(stderr, "EPIPE");
        Close(proxyfd);
        Free(cachebuf);
        return;
      } else {
        perror("another");
        Close(proxyfd);
        Free(cachebuf);
        return;
      }
    }
  }
  if (byte_cnt <= MAX_OBJECT_SIZE) {
    insertCache(&cache, url, cachebuf, byte_cnt);
    printf("insert to cache\n");
  }
  Close(proxyfd);
  Free(cachebuf);
  return;
}

struct socketinfo parse_url(char *url) {
  char cpy[MAXLINE];
  strcpy(cpy, url);
  struct socketinfo result = {"0", "80", "0"};
  char *two_backslash = strstr(url, "//");
  char *tem = strchr(two_backslash + 2, '/');
  char *buf = strchr(two_backslash + 2, ':');
  strcpy(result.uri, tem);
  if (buf) {
    *tem = '\0';
    strcpy(result.port, buf + 1);
    *buf = '\0';
    strcpy(result.hostname, two_backslash + 2);
  } else {
    *tem = '\0';
    strcpy(result.hostname, two_backslash + 2);
    strcpy(result.port, "80");
  }
  strcpy(url, cpy);
  return result;
}

void *thread(void *vargp) {
  pthread_detach(pthread_self());
  while (1) {
    int connfd = sbuf_remove(&sbuf);
    doit(connfd);
    Close(connfd);
  }
  return NULL;
}
