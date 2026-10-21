#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* struct for a cached object 
 key: filename
 header: buffer for response header
 bodyp: pointer to space for respone body
 prev: pointer to the object which is just after cached (towards head)
 next: pointer to the object which is just before cached (towards tail)
*/
typedef struct obj_c{
  char key[MAXLINE];
  char header[MAXLINE];
  char *bodyp;
  struct obj_c *prev;
  struct obj_c *next;
} obj_c;

/* struct for a cache(doubly linked list) 
 * head: pointer to the most recently used object
 * tail: pointer to the least recently used object
 * obj_cnt: the number of objects in cache
 *          (max_number of objs in a cache is 10(~= MAX_CACHE_SIZE / MAX_OBJECT_SIZE))
*/
typedef struct{
  obj_c *head;
  obj_c *tail; 
  int obj_cnt;
} cache;

/* arguments for function 'thread' */
typedef struct{
  int fd; /* connfd for a proxy which connects with a client */
  cache* c; /* shared memory space between mutiple threads */
} thread_arg;


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* function prototypes */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_uri(char *uri, char *filename, char *cgiargs);
int read_responsehdrs(rio_t *rp, int fd, char* headerp);
void read_requesthdrs(rio_t *rp, char* h_host, char* h_port);
void doit(cache *c,int fd);
int parse_requestline(char *buf, char *method, char *hostname, char *port, char *uri, char *filename, char *version);
void do_request(int fd, char *hostname, char *method, char *filename);
void do_response(cache *c, char *filename, rio_t *rio, int fd, char *bodyp);
int is_valid(int fd, char *method, char *hostname, char *port, char *version, char *h_host, char *h_port);
void *thread(void *vargp);
void delete_obj(cache *c, obj_c *target);
obj_c *is_cached(cache *c, char *target);
cache *new_cache(void);
void dump_cache(cache *c);
obj_c *insert_obj(cache *c, char *filename, char *headerp, char *bodyp);
void remove_cache_obj(obj_c *obj);

/* read_requesthdrs
 * 1. read http request header
 * 2. parse ip address(hostname) and portnumber
 *    for checking whether 'hostname:portnumber' of Host header value and one of request line differ
 */
void read_requesthdrs(rio_t *rp, char* h_host, char* h_port){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  {
    if (strstr(buf, "Host: ")){
      sscanf(buf, "%*[^:]%*[: ]%[^:]:%s", h_host, h_port);
    }
    Rio_readlineb(rp, buf, MAXLINE);
  }
  return;
}

/* read_responsehdrs
 * 1. read response header, which transfered from a server
 * 2. parse Content-length to figure out size of response body
 * 3. buffer response header lines to 'header'
 * 4. write them to fd(connfd)
 * 5. return Content-length
*/
int read_responsehdrs(rio_t *rp, int fd, char* header){
  char content_length[MAXLINE], buf[MAXLINE], headerbuf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  sprintf(headerbuf, "%s", buf);
  while (strcmp(buf, "\r\n")){
    Rio_readlineb(rp, buf, MAXLINE);
    if (strstr(buf, "Content-length: ")){
      sscanf(buf, "%*[^:]:%s", content_length);
      sprintf(headerbuf, "%sContent-length: %s\r\n", headerbuf, content_length);
      continue;
    }
    sprintf(headerbuf, "%s%s", headerbuf,buf);
  }
  strcpy(header, headerbuf);
  Rio_writen(fd, header, strlen(header));
  return atoi(content_length);
}

/* parse_requestline
 * parse method, hostname, port, filename, version from a request line
    by using regular expressions.
*/
int parse_requestline(char *buf, char *method, char* hostname, char* port, char*uri, char *filename, char *version){
  char host_n_port[MAXLINE];
  
  sscanf(buf, "%s%*[ htp:/]%[^ ]%s", method, uri, version);
  sscanf(uri, "%[^/]%s", host_n_port, filename);
  sscanf(host_n_port, "%[^:]:%s", hostname, port);

  /* set defualt port number and filename, if needed.*/
  if (strlen(port) == 0)
    strcpy(port, "80");
  if(strlen(filename) == 0)
    strcpy(filename, "/home.html");
  return 0;
}

/* clienterror
 * 1. show error messages to client
   2. if client requests invalid request, 
      proxy responses with clienterror and does not communicate with server.
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg){
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor= "
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Proxy</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}


/* is_valid
 * 1. proxy checks requests from clients is valid or not.
   2. in this assignment, 
      1) only 'GET' method is supported
      2) HTTP/1.1 request must bring Host header, which is identical with hostname:port in a request line
   3. if valid, return 0
   4. otherwise, return -1
*/
int is_valid(int fd,char *method, char*hostname, char*port, char*version, char* h_host, char* h_port){
  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not implemented", "Proxy does not implement this method");
    return -1;
  }
  if (!strcasecmp(version, "HTTP/1.1")){
    if (strlen(h_host) > 0){
      if (strcasecmp(h_host, hostname)){
        clienterror(fd, hostname, "400", "Bad Request", "Hostname of request line and of host header differ");
        return -1;
      }
      if (strlen(h_port) != 0 && strcasecmp(h_port, port)){
        clienterror(fd, hostname, "400", "Bad Request", "Port number of request line and of host header differ");
        return -1;
      }
      return 0;
    }
    clienterror(fd, version, "400", "Bad Request", "HTTP/1.1 requires Host header");
    return -1;
  }
  return 0;
}

/* do_request:
 * 1. write request line to server
   2. in this assignment, 
    1) a request line (with HTTP/1.0)
    2) Host header
    3) user_agent header
    4) Connection header
    5) Proxy_connection header 
    must be included in a requet header
 */
void do_request(int fd, char*hostname,char*method, char*filename){
  char buf[MAXLINE];
  /* request line */
  sprintf(buf, "%s %s HTTP/1.0\r\n", method, filename);
  Rio_writen(fd, buf, strlen(buf));
  /* host header */
  sprintf(buf, "Host: %s\r\n", hostname);
  Rio_writen(fd, buf, strlen(buf));
  /* user_agent header */
  sprintf(buf, "%s",user_agent_hdr);
  Rio_writen(fd, buf, strlen(buf));
  /* connection header */
  sprintf(buf, "Connection: close\r\n");
  Rio_writen(fd, buf, strlen(buf));
  /* proxy_connection header */
  sprintf(buf, "Proxy-Connection: close\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));
}

/* do_response:
 * 1. read respone header and body
   2. if response message does not include error status and is smaller than MAX_OBJECT_SIZE
   3. then, cache it.
*/
void do_response(cache* c, char* filename, rio_t* rio, int fd, char* bodyp){
  int content_length;
  char headerbuf[MAXLINE];

  content_length = read_responsehdrs(rio, fd, headerbuf);
  bodyp = (char *)Malloc(content_length);
  Rio_readnb(rio, bodyp, content_length);
  Rio_writen(fd, bodyp, content_length);
  if(!strstr(bodyp, "404") && !strstr(bodyp, "403")){
    if (strlen(headerbuf) + content_length <= MAX_OBJECT_SIZE){
      if (c->obj_cnt<10){
        insert_obj(c, filename, headerbuf, bodyp);
      }else{
        delete_obj(c, c->tail->prev);
        insert_obj(c, filename, headerbuf, bodyp);
      }
    }
  }
  
}

/* doit
 * 1. read a request message from client
 * 2. check it is valid or not
 * 3. if it is alread cached, get it and response client with it.
 * 4. otherwise, make a request message and send it to server
 */
void doit(cache * c, int fd)
{
  char buf[MAXLINE], method[MAXLINE], hostname[MAXLINE], port[MAXLINE], filename[MAXLINE], uri[MAXLINE], version[MAXLINE], h_host[MAXLINE], h_port[MAXLINE];
  rio_t rio;
  int clientfd, content_length;
  char body[MAX_OBJECT_SIZE], header[MAXLINE];
  obj_c *obj;

  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);

  parse_requestline(buf, method, hostname, port, uri, filename, version);
  read_requesthdrs(&rio, h_host, h_port);

  // valid checking
  if (is_valid(fd, method, hostname, port, version, h_host, h_port) == -1)
    return;

  if ((obj = is_cached(c, filename))!= NULL){ // cached one
    Rio_writen(fd, obj->header, strlen(obj->header));
    Rio_writen(fd, obj->bodyp, MAX_OBJECT_SIZE);
    if (obj != c->head->next){
      delete_obj(c, obj);
      insert_obj(c, filename, obj->header, obj->bodyp);
    }
  }else{ // un-cached one
    clientfd = Open_clientfd(hostname, port);
    do_request(clientfd, hostname, method, filename);
    Rio_readinitb(&rio, clientfd);
    do_response(c, filename, &rio, fd, body);
    Close(clientfd);
  }
  return;
}

/* thread
 * make a routine for one thread
 */
void *thread(void *vargp){
  int connfd = ((thread_arg *)vargp)->fd;
  cache *c = ((thread_arg *)vargp)->c;
  Free(vargp);
  Pthread_detach(pthread_self());
  doit(c, connfd);
  Close(connfd);
  return NULL;
}

/* new_cache
 * make and init a cache
 */
cache *new_cache(void){
  cache *c = (cache *)calloc(1, sizeof(cache));
  c->head = (obj_c *)calloc(1, sizeof(obj_c));
  c->tail = (obj_c *)calloc(1, sizeof(obj_c));
  c->obj_cnt = 0;
  return c;
}

/* is_cached
 * 1. find out a requested object is already cached or not, by comparing filename with object->key
   2. if so, return the object
   3. otherwise, return NULL
 */
obj_c* is_cached(cache * c, char* filename){
  obj_c *ptr;
  for (ptr = c->head; ptr != NULL; ptr = ptr->next){
    if (!strcasecmp(ptr->key, filename)){
      return ptr;
    }
  }
  return NULL;
}

/* insert_obj
 * set key, header, bodyp, next, prev members
 * if the number of objects in a cache, 
    then tail will be used for next deletion
*/
obj_c* insert_obj(cache *c, char* filename ,char* headerp ,char* bodyp){
  obj_c *n = (obj_c *)calloc(1, sizeof(obj_c));
  strcpy(n->key,filename);
  strcpy(n->header , headerp);
  n->bodyp = bodyp;
  n->next = c->head->next;
  c->head->next = n;
  c->obj_cnt += 1;
  if (c->obj_cnt == 10){ 
    c->tail->prev = n;
    n->next = c->tail;
  }
  return n;
}

/* delete_obj
 * delete one object from a cache
*/
void delete_obj(cache *c, obj_c* target){
  if (target == c->tail->prev){
    target->prev->next = c->tail;
    c->tail->prev = target->prev;
    Free(target->bodyp);
    Free(target);
  }
  else if (target != c->head->next)
  {
    target->prev->next = target->next;
    target->next->prev = target->prev;
    Free(target->bodyp);
    Free(target);
  }
  c->obj_cnt -= 1;
}

/* remove_cache_obj
 * remove recursively cached objects
 */
void remove_cache_obj(obj_c* obj){
  if (obj->next != NULL){
    remove_cache_obj(obj->next);
  }
  Free(obj->bodyp);
  Free(obj);
}

int main(int argc, char*argv[]) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* Open listen socket */
  listenfd = Open_listenfd(argv[1]);
  cache *proxy_cache = new_cache();
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    thread_arg *arg = (thread_arg *)Malloc(sizeof(thread_arg));
    arg->fd = connfd;
    arg->c = proxy_cache;
    Pthread_create(&tid, NULL, thread, arg);
  }
  remove_cache_obj(proxy_cache->head);
  Free(proxy_cache);
}