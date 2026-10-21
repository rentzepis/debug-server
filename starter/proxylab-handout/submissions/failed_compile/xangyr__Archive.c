#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "csapp.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
//const char *conn_hdr = "close";
//const char *prxy_conn_hdr = "close";

/*
 clienterror - returns error message to client(copy from tiny.c)
 error handler, write to file descripter what kind of error with messages
 */
void error_handle(int fd, char *cause,char *errnum, 
  char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];
  
  /* Build the HTTP response body */
  sprintf(body, "<html><title>There's an Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>Error print page</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  sprintf(buf, "%sContent-type: text/html\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}

/*
 parse_url - get host port path
 check if proper, then copy each part to pointers
 */
int parse_url(const char *url, char *host, char *port, char *path) {
  /*
   check the first 7 characters of url to see if proper
   add return 1 at the end of the if statement
   add return 0 outside the statement
   */
  if (!strncasecmp(url, "http://", 7)) {
    /*
     create variables:
     'temp' to store the string of url
        since url is 'const char', and arguments of strtok need to be 'char'
     'token' to split
     'rest' to store the rest string
     */
    char temp[MAXBUF];
    strcpy(temp, url);
    char *token;
    char *rest = temp;
    
    /*
     after testing, by using 2 time strtok(string,"/")
     host:port is stored in 'token' and the path is stored in 'rest'
     so split 'token' by ":" again and copy to host
     */
    token = strtok_r(rest, "/", &rest);
    token = strtok_r(NULL, "/", &rest);
    
    strcpy(host, strtok_r(token, ":", &token));
    
    /*
     check if strtok return NULL meaning there is no ':port_#' so copy 80 to port
     else copy 'token' to port
     */
    if((token = strtok(token, ":")) == NULL) {
      strcpy(port, "80");
    } else {
      strcpy(port, token);
    }
    
    /*
     by requirement the path should contain "/", so copy to path first
     then use strcat to append the string to the dest
     don't need to care if the 'rest' is empty
     */
    strcpy(path, "/");
    strcat(path, rest);
    
    return 1;
  }
  return 0;
}

/*
 request_hdrs - copy each headers to (final) headers
 read each line from client, copy request, host, connection, proxy
 and other headers to headers
 */
void get_hdrs(rio_t *rp, char *host, char *path, char *headers, int fd){
  /*
   create buffer for reading
   'request_header' 'host_header' 'other' to hold
   */
  char buf[MAXLINE];
  char request_hdr[MAXLINE];
  char host_hdr[MAXLINE];
  char other[MAXLINE];
  //rio read line
  while (rio_readlineb(rp, buf, MAXLINE) > 0) {
    //when reach EOF, break loop
    if (!strcmp(buf, "\r\n")) {
      break;
    }
    //copy line to host header
    if (!strncasecmp(buf, "Host", strlen("Host"))) {
      strcpy(host_hdr, buf);
      continue;
    }
    //copy rest to other, without following headers
    else if (!strncasecmp(buf, "User-Agent", strlen("User-Agent")) ||
      !strncasecmp(buf, "Connection", strlen("Connection")) || 
      !strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")) || 
      !strncasecmp(buf, "If-Modified-Since", strlen("If-Modified-Since")) || 
      !strncasecmp(buf, "If-None-Match", strlen("If-None-Match"))) {
      continue;
    }
    strcat(other, buf);
  }
  /*
   if doesn't provide host header
    status code 400 (Bad Request)
    format host header
   */
  if (!strlen(host_hdr)) {
    error_handle(fd, host, "400", "Bad Request", 
      "Sent Bad Request, Proxy may not respond.");
    sprintf(host_hdr, "Host: %s\r\n", host);
  }
  //formats for each header
  char *format = "%s%s%s%sConnection: close\r\nProxy-Connection: close\r\n\r\n";

  sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);
  //format (final) headers
  sprintf(headers, format, request_hdr, user_agent_hdr, host_hdr,
    other);
}

/*
 to_client - send back server response to client, check to cache
 */
void to_client(rio_t *rp, char *uri, CacheList *cache_l, 
  int clnt_fd, int srvr_fd, char *host) {
  /*
   flg_stts:  check flag for status code
   flg_len:   check flag for content length
   cntnt_len: integer to hold content length from server response

   size: actual read size
   */
  char buf[MAX_OBJECT_SIZE];
  char *headers = malloc(MAX_OBJECT_SIZE);;
  int flg_stts = 0, flg_len = 0, cntnt_len = -1;
  size_t size;

  memset(&buf, 0, sizeof(buf));
  rio_readinitb(rp, srvr_fd);
  //when read nothing or failed
  if ((size = rio_readlineb(rp, buf, MAX_OBJECT_SIZE)) <= 0) {
    if (!size) {
      error_handle(clnt_fd, host, "503",
        "Service Unavailable", 
        "Server is not ready to handle the request.");
    }
    else {
      error_handle(clnt_fd, host, "500",
        "Internal Server Error", 
        "Server Encountered an unexpected condition.");
    }
    free(headers);
    return;
  }
  //read every line of headers till EOF, append to headers
  while (1) {
    if (!strcmp(buf, "\r\n")) {
      strcat(headers, buf);
      rio_writen(clnt_fd, buf, size);
      break;
    }
    //check status code == 200
    if (!strcasecmp(buf, "HTTP/1.0 200 OK\r\n")) {
      flg_stts = 1;
    }
    //check content length provided, split and store in cntnt_len
    if (!strncasecmp(buf, "Content-length:", strlen("Content-length:"))) {
      char temp[MAX_OBJECT_SIZE];
      char *rest;
      strcpy(temp, buf);
      strtok_r(temp, ":", &rest);
      sscanf(rest,"%d", &cntnt_len);
      flg_len = 1;
    }
    
    strcat(headers, buf);
    /*
     send response back to client
     read next line, update read size
     append '\r\n' as last line of response
     */
    rio_writen(clnt_fd, buf, size);
    size = rio_readlineb(rp, buf, MAX_OBJECT_SIZE);
  }

  /*
   prepare for caching
   robustly read n bytes content and send to client
   close connection & return
   */
  void *item = malloc(MAX_OBJECT_SIZE);
  size = rio_readnb(rp, item, MAX_OBJECT_SIZE);
  rio_writen(clnt_fd, item, size);
  
  //check flags & if read size match provided content length, cache
  if (flg_len && flg_stts && size == cntnt_len) {
    cache_URL(uri, headers, item, size, cache_l);
  }
  else {
    free(headers);
    free(item);
  }
  close(srvr_fd);
  return;
}

void doit(int fd, CacheList *cache_l) {
  
  char headers[MAXLINE], buf[MAXLINE], method[MAXLINE], uri[MAXLINE], 
    version[MAXLINE], host[MAXLINE], path[MAXLINE], port[MAXLINE];
  rio_t bwsr_io, srvr_io;

  
  memset(&buf, 0, sizeof(buf));
  memset(&host, 0, sizeof(host));
  memset(&path, 0, sizeof(path));
  memset(&port, 0, sizeof(port));
  
  rio_readinitb(&bwsr_io, fd);
  /*
   store string from client into buffer
   if nothing, return
   else print out then handle
  */
  if (!rio_readlineb(&bwsr_io, buf, MAXLINE)) {
    return;
  }
  //printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  //if not GET, request error
  if (strcasecmp(method, "GET")) {
    error_handle(fd, method, "501", "Not Implemented", 
      "Proxy doesn't implement this Method.");
    return;
  }
  //if not proper url, url error
  if(!parse_url(uri, host, port, path)) {
    error_handle(fd, host, "404", "Not found", "File not found.");
    return;
  }
  CachedItem *cache = find(uri, cache_l);
  int srvr_fd = Open_clientfd(host, port);

  if(cache) {
    //found in cache list, get from cache list directly
    rio_writen(fd, cache->headers, strlen(cache->headers));
    rio_writen(fd, cache->item_p, cache->size);
    return;
  }
  //format into headers
  get_hdrs(&bwsr_io, host, path, headers, fd);

  
  /*
    connection success
    init, send headers to server, send back to client
  */
  rio_readinitb(&srvr_io, srvr_fd);
  rio_writen(srvr_fd, headers, strlen(headers));
  to_client(&srvr_io, uri, cache_l, fd, srvr_fd, host);
  
  return;
}

int main(int argc, char **argv) {
  CacheList cache;
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  
  cache_init(&cache);
  
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  
  Signal(SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd, &cache);
    close(connfd);
  }
  cache_destruct(&cache);
  return 0;
}
