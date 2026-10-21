#include "csapp.h"
#include <stdio.h>
#include <stdbool.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Header Strings */
static const char *header_user_agent = "Mozilla/5.0"
                                    " (X11; Linux x86_64; rv:45.0)"
                                    " Gecko/20100101 Firefox/45.0";
static const char *header_conn = "Conection: close\r\n";
static const char *header_proxy_conn = "Proxy-Connection: close\r\n\r\n";


/* Function Prototypes */
void *thread(void *vargp);
void proxy(int fd);
void parse_uri(char *uri, char *host, char *port, char *path);
bool read_requesthdrs(rio_t *rp);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);


int main(int argc, char** argv) {
  int listenfd, *connfdp;
  socklen_t  clientlen;
  struct sockaddr_in clientaddr;
  pthread_t tid;

  /* check for correct number of args */
  if (argc != 2) {
    printf("Incorrect number of arguments");
    exit(0);
  }

  listenfd = Open_listenfd(argv[1]);
  clientlen = sizeof(struct sockaddr_in);

  while (1) {
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *) &clientaddr, &clientlen);
    Pthread_create(&tid, NULL, thread, connfdp);
  }

  return 0;
}

/*
 * thread is the thread routine passed into Pthread_create and is called
 * in the execution of a new thread
 */
void *thread(void *vargp)
{
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  proxy(connfd);
  Close(connfd);
  return NULL;
}

/*
 * proxy handles responses
 */
void proxy(int fd)
{
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[MAXLINE], path[MAXLINE], s_buf[MAXLINE];
  int serverfd;
  rio_t rio, rio_server;
  size_t bytes;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);

  printf("Request:\n");
  printf("%s", buf);

  sscanf(buf, "%s %s %s", method, uri, version);

  if (strcmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
    return;
  }

  read_requesthdrs(&rio);

  /* Parse URI from GET request */
  parse_uri(uri, host, port, path);

  serverfd = Open_clientfd(host, port);
  Rio_readinitb(&rio_server, serverfd);



  /* Forward Request */
  sprintf(s_buf, "GET %s HTTP/1.0\r\n", path);
  Rio_writen(serverfd, s_buf, strlen(s_buf));
  sprintf(s_buf, "Host: %s\r\n", host);
  Rio_writen(serverfd, s_buf, strlen(s_buf));
  Rio_writen(serverfd, (void*)header_user_agent, strlen(header_user_agent));
  Rio_writen(serverfd, (void*)header_conn, strlen(header_conn));
  Rio_writen(serverfd, (void*)header_proxy_conn, strlen(header_proxy_conn));

  /* Forward Response */
  while((bytes = Rio_readlineb(&rio_server, s_buf, MAXLINE)) != 0) {
    Rio_writen(fd, s_buf, bytes);
  }
}

/*
 * parse_uri parses the given uri to obtain the given host, port, and path
 */
void parse_uri(char *uri, char *host, char *port, char *path)
{
  char *test;
  char *ptr;
  char http[MAXLINE];
  int len = 0;
  int len_p = 0;
  int len_path = 0;

  ptr = uri;

  // make sure that the uri does not exceed buffer
  test = strchr(ptr, '/');
  if (NULL == test) {
    printf("Improper URI");
    return;
  }

  // verify that the uri begins with "http://"
  (void)strncpy(http, &uri[0], 7);
  http[7] = '\0';

  if ((strcmp(http, "http://"))) {
    printf("URI is not http");
    return;
  }


  // skip to after "http://"
  test += 2;

  // get host field
  while(*test != '\0') {
    if (*test == '/' || *test == ':') {
      break;
    }
    test++;
    len++;
  }

  (void)strncpy(host, &uri[7], len);
  host[len] = '\0';

  len += 7;

  // return port

  // default port
  if (*test == '/'){
    strcpy(port, "80");
  }
  // specified port
  else {
    while(*test != '\0') {
      if (*test == '/') {
        break;
      }
      test++;
      len_p++;
    }

    (void)strncpy(port, &uri[len+1], len_p - 1);
    port[len_p - 1] = '\0';
  }

  len += len_p;

  //get path if it exists
  if (*test == '\0') {
    strcpy(path, "/");
  }
  else {
    while(*test != '\0') {
      test++;
      len_path++;
    }

    (void)strncpy(path, &uri[len], len_path);
    path[len_path] = '\0';
  }
}


/*
 * read_requesthdrs - read HTTP request headers
 * Returns true if an error occurred, or false otherwise.
 */
bool read_requesthdrs(rio_t *rp) {
    char buf[MAXLINE];

    do {
        if (rio_readlineb(rp, buf, MAXLINE) <= 0) {
            return true;
        }

        printf("%s", buf);
    } while(strncmp(buf, "\r\n", sizeof("\r\n")));

    return false;
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum,
        char *shortmsg, char *longmsg) {
    char buf[MAXLINE];
    char body[MAXBUF];
    size_t buflen;
    size_t bodylen;

    /* Build the HTTP response body */
    bodylen = snprintf(body, MAXBUF,
            "<!DOCTYPE html>\r\n" \
            "<html>\r\n" \
            "<head><title>Tiny Error</title></head>\r\n" \
            "<body bgcolor=\"ffffff\">\r\n" \
            "<h1>%s: %s</h1>\r\n" \
            "<p>%s: %s</p>\r\n" \
            "<hr /><em>The Tiny Web server</em>\r\n" \
            "</body></html>\r\n", \
            errnum, shortmsg, longmsg, cause);
    if (bodylen >= MAXBUF) {
        return; // Overflow!
    }

    /* Build the HTTP response headers */
    buflen = snprintf(buf, MAXLINE,
            "HTTP/1.0 %s %s\r\n" \
            "Content-Type: text/html\r\n" \
            "Content-Length: %zu\r\n\r\n", \
            errnum, shortmsg, bodylen);
    if (buflen >= MAXLINE) {
        return; // Overflow!
    }

    /* Write the headers */
    if (rio_writen(fd, buf, buflen) < 0) {
        fprintf(stderr, "Error writing error response headers to client\n");
        return;
    }

    /* Write the body */
    if (rio_writen(fd, body, bodylen) < 0) {
        fprintf(stderr, "Error writing error response body to client\n");
        return;
    }
}

