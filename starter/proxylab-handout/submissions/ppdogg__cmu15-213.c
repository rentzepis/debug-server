#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct cache_object {
  char *key;  // key
  char *buf;  // data
  size_t len; // length of data

  struct cache_object *prev;
  struct cache_object *next;
};

void start(char *port);
void parse(int client_sd);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void build_new_request_hdr(rio_t *real_client, char *new_request,
                           char *hostname, char *port);
void *thread(void *vargp);
void init_cache();
size_t get(char *key, char *value);
void put(char *key, char *value, size_t len);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_hdr = "Proxy-Connection: close\r\n";

static const char *host = "127.0.0.1";
static char *port = "19432";

/* Cache object */
struct cache_object *head;
struct cache_object *tail;
static int cache_size;
pthread_rwlock_t lock; // lock

int main(int argc, char **argv) {
  printf("%s", user_agent_hdr);

  // init cache
  init_cache();

  if (argc < 2) {
    start(port);
  } else {
    start(argv[1]);
  }

  pthread_rwlock_destroy(&lock);
  return 0;
}

void start(char *port) {
  int listen_sd, *conn_sd;
  struct sockaddr_storage ca;
  socklen_t addr_len;
  pthread_t tid;

  listen_sd = Open_listenfd(port);
  printf("Start listening on port %s\n", port);

  addr_len = sizeof(struct sockaddr_storage);
  while (1) {
    conn_sd = Malloc(sizeof(int));
    *conn_sd =
        Accept(listen_sd, (struct sockaddr *)&ca, (socklen_t *)&addr_len);
    Pthread_create(&tid, NULL, thread, conn_sd);
  }

  Close(listen_sd);
}

void parse(int client_sd) {
  int real_server_sd, port, char_nums;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], path[MAXLINE], port_str[10];
  char new_request[MAXLINE];
  rio_t real_server, real_client;
  int cache_len;
  char *cache_data;
  char tmp[MAXLINE];

  // cache data
  cache_len = 0;
  cache_data = Malloc(MAX_OBJECT_SIZE);

  Rio_readinitb(&real_client, client_sd);
  Rio_readlineb(&real_client, buf, MAXLINE);
  printf("client request are: \n%s", buf);

  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET") != 0) {
    clienterror(client_sd, method, "501", "Not Implemented",
                "Tiny server does not implement this method");
    return;
  }

  parse_uri(uri, hostname, path, &port);
  sprintf(port_str, "%d", port);
  printf("request uri are:\nhostname: %s, path: %s, port: %d\n", hostname, path,
         port);
  if ((cache_len = get(path, cache_data)) != 0) {
    Rio_writen(client_sd, cache_data, cache_len);
    Free(cache_data);
    return;
  }

  // build new request to real server
  sprintf(new_request, "GET %s HTTP/1.0\r\n", path);
  build_new_request_hdr(&real_client, new_request, hostname, port_str);

  // send request to real server
  real_server_sd = Open_clientfd(hostname, port_str);
  Rio_writen(real_server_sd, new_request, strlen(new_request));

  Rio_readinitb(&real_server, real_server_sd);
  while ((char_nums = Rio_readlineb(&real_server, buf, MAXLINE)) > 0) {
    Rio_writen(client_sd, buf, char_nums);

    if (cache_len + char_nums < MAX_OBJECT_SIZE) {
      memcpy(cache_data + cache_len, buf, char_nums);
    }
    cache_len += char_nums;
  }

  if (cache_len < MAX_OBJECT_SIZE) {
    put(path, cache_data, cache_len);
  } else {
    Free(cache_data);
  }
}

/*
 * clienterror - returns an error message to the client
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body,
          "%s<body bgcolor="
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

/*
 * parse_uri - parse uri to get hostname, port, path from real client
 */
void parse_uri(char *uri, char *hostname, char *path, int *port) {
  *port = 80; /* default port */
  char *ptr_hostname = strstr(uri, "//");
  /* normal uri => http://hostname:port/path */
  /* eg. uri => http://www.cmu.edu:8080/hub/index.html */
  if (ptr_hostname)
    /* hostname_eg1. uri => http://hostname... */
    ptr_hostname += 2;
  else
    /* hostname_eg2. uri => hostname... <= NOT "http://"*/
    ptr_hostname = uri;

  char *ptr_port = strstr(ptr_hostname, ":");
  /* port_eg1. uri => ...hostname:port... */
  if (ptr_port) {
    *ptr_port = '\0'; /* c-style: the end of string(hostname) is '\0' */
    strncpy(hostname, ptr_hostname, MAXLINE);

    /* change default port to current port */
    /* if path not char, sscanf will automatically store the ""(null) int the
     * path */
    sscanf(ptr_port + 1, "%d%s", port, path);
  }
  /* port_eg1. uri => ...hostname... <= NOT ":port"*/
  else {
    char *ptr_path = strstr(ptr_hostname, "/");
    /* path_eg1. uri => .../path */
    if (ptr_path) {
      *ptr_path = '\0';
      strncpy(hostname, ptr_hostname, MAXLINE);
      *ptr_path = '/';
      strncpy(path, ptr_path, MAXLINE);
      return;
    }
    /* path_eg2. uri => ... <= NOT "/path"*/
    strncpy(hostname, ptr_hostname, MAXLINE);
    strcpy(path, "");
  }
  return;
}

/*
 * build_new_request_hdr - get old request_hdr then build new request_hdr
 */
void build_new_request_hdr(rio_t *real_client, char *new_request,
                           char *hostname, char *port) {
  char temp_buf[MAXLINE];

  /* get old request_hdr */
  while (Rio_readlineb(real_client, temp_buf, MAXLINE) > 0) {
    if (strstr(temp_buf, "\r\n"))
      break; /* read to end */

    /* if all old request_hdr had been read, we get it */
    if (strstr(temp_buf, "Host:"))
      continue;
    if (strstr(temp_buf, "User-Agent:"))
      continue;
    if (strstr(temp_buf, "Connection:"))
      continue;
    if (strstr(temp_buf, "Proxy Connection:"))
      continue;

    sprintf(new_request, "%s%s", new_request, temp_buf);
  }

  /* build new request_hdr */
  sprintf(new_request, "%sHost: %s:%s\r\n", new_request, hostname, port);
  sprintf(new_request, "%s%s%s%s", new_request, user_agent_hdr, conn_hdr,
          proxy_hdr);
  sprintf(new_request, "%s\r\n", new_request);
}

/*
 *Thread routine
 */
void *thread(void *vargp) {
  int conn_sd = *((int *)vargp);
  Pthread_detach(Pthread_self());
  Free(vargp);
  parse(conn_sd);
  Close(conn_sd);
  return NULL;
}

void init_cache() {
  cache_size = 0;

  head = Malloc(sizeof(struct cache_object));
  tail = Malloc(sizeof(struct cache_object));

  head->next = tail;
  head->prev = NULL;
  tail->prev = head;
  tail->next = NULL;

  pthread_rwlock_init(&lock, NULL);
}

size_t get(char *key, char *data) {
  size_t len;
  struct cache_object *ptr;

  pthread_rwlock_rdlock(&lock);
  ptr = head->next;
  while (ptr != tail) {
    if (strcmp(ptr->key, key) == 0) {
      pthread_rwlock_unlock(&lock);
      pthread_rwlock_wrlock(&lock);
      // move to front
      ptr->prev->next = ptr->next;
      ptr->next->prev = ptr->prev;

      ptr->prev = head;
      ptr->next = head->next;
      ptr->prev->next = ptr;
      ptr->next->prev = ptr;

      len = ptr->len;
      memcpy(data, ptr->buf, ptr->len);
      pthread_rwlock_unlock(&lock);
      return len;
    }
    ptr = ptr->next;
  }
  pthread_rwlock_unlock(&lock);

  return 0;
}

void put(char *key, char *value, size_t len) {
  struct cache_object *ptr;

  while (cache_size + len > MAX_CACHE_SIZE) {
    pthread_rwlock_wrlock(&lock);
    ptr = tail->prev;
    cache_size -= strlen(ptr->buf);

    ptr->prev->next = ptr->next;
    ptr->next->prev = ptr->prev;
    pthread_rwlock_unlock(&lock);

    Free(ptr->key);
    Free(ptr->buf);
    Free(ptr);
  }

  cache_size += len;
  ptr = Malloc(sizeof(struct cache_object));

  ptr->key = Malloc(strlen(key));
  strncpy(ptr->key, key, strlen(key));
  ptr->buf = value;
  ptr->len = len;

  pthread_rwlock_wrlock(&lock);
  ptr->prev = head;
  ptr->next = head->next;
  ptr->prev->next = ptr;
  ptr->next->prev = ptr;
  pthread_rwlock_unlock(&lock);
}