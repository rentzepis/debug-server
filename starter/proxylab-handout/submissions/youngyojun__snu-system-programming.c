/*
 * proxy.c - Caching HTTP Web Proxy
 *
 * 2020-14378
 * stu70@sp01.snucse.org
 *
 * Gyojun Youn
 * youngyojun [at] snu.ac.kr
 *
 * College of Engineering
 * Dept. of Computer Science and Engineering
 *
 */

#include "csapp.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>


/* Recommended max cache and object sizes. */
#define MAX_CACHE_SIZE  (1049000)
#define MAX_OBJECT_SIZE (102400)

/* Max value of a valid port. */
#define MAX_PORT  (65535)


/* Compute the size of a cache entry of res_cache. */
#define GET_CACHE_ALLOC_SIZE(res_cache)   (\
    ((res_cache) ? (res_cache)->totsz : 0)\
    + sizeof(request_line)\
    + sizeof(cache_entry)\
  )


/*
 * free_list - Free the entire list
 *    starts with __argp.
 *    Each list entry must have a pointer nxt.
 */
#define free_list(__argp) {\
    typeof(__argp) __p = (__argp);\
    void *__fp;\
    while (__p) {\
      __fp = __p;\
      __p = __p->nxt;\
      free(__fp);\
    }\
    (__argp) = NULL;\
  }


/*
 * copy_list - Copy the entire list
 *    starts with __argp
 *    and save them in __retp.
 *    The list must be linear (not circular).
 *    If something goes wrong,
 *    then __retp will be nullptr.
 */
#define copy_list(__retp, __argp) {\
    typeof(__retp) __p = (__argp);\
    typeof(__retp) __q = NULL;\
    int __failed_flag = 0;\
    (__retp) = NULL;\
    while (__p) {\
      typeof(__retp) __v = (typeof(__retp))malloc(sizeof(typeof(*__p)));\
      if (!__v) {\
        __failed_flag = 1;\
        break;\
      }\
      memcpy(__v, __p, sizeof(typeof(*__p)));\
      if (!(__retp))\
        (__retp) = __v;\
      if (__q)\
        __q->nxt = __v;\
      __q = __v;\
      __p = __p->nxt;\
    }\
    if (__failed_flag) {\
      free_list((__retp));\
      (__retp) = NULL;\
    }\
  }


/*
 * access_read_cache - Wait until
 *    being able to read the cache
 *    and do __func.
 * Ref: 3rd readers-writers problem.
 */
#define access_read_cache(__func)  {\
    P(&(cache_sems.sem_in));\
    cache_sems.incnt += 1;\
    V(&(cache_sems.sem_in));\
    { (__func); }\
    P(&(cache_sems.sem_out));\
    cache_sems.outcnt += 1;\
    if (1 == cache_sems.wait\
      && cache_sems.incnt == cache_sems.outcnt)\
        V(&(cache_sems.sem_w));\
    V(&(cache_sems.sem_out));\
  }


/*
 * access_write_cache - Wait until
 *    being able to write the cache
 *    and do __func.
 * Ref: 3rd readers-writers problem.
 */
#define access_write_cache(__func)  {\
    P(&(cache_sems.sem_in));\
    P(&(cache_sems.sem_out));\
    if (cache_sems.incnt == cache_sems.outcnt)\
      V(&(cache_sems.sem_out));\
    else {\
      cache_sems.wait = 1;\
      V(&(cache_sems.sem_out));\
      P(&(cache_sems.sem_w));\
      cache_sems.wait = 0;\
    }\
    { (__func); }\
    V(&(cache_sems.sem_in));\
  }


/* Important string constants. */
static const char* const CRLF = "\r\n";
static const char* const CLNSP = ": ";
static const char* const HDR_USR_AGT = "User-Agent";
static const char* const HDR_CONN = "Connection";
static const char* const HDR_PRX_CONN = "Proxy-Connection";
static const char* const HDR_HOST = "Host";
static const char* const HDR_DATA_CLOSE = "close";
static const char* const HDR_DATA_USR_AGT = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";


/* Hash values of request_line. */
typedef struct request_line_hash {
  uint32_t hostname;
  uint32_t path;
} request_line_hash;


/*
 * RFC 1945 Request-Line.
 * Method SP Request-URI SP HTTP-Version CRLF
 */
typedef struct request_line {
  /* Method must be "GET". */
  /* Request-URI will be parsed as below. */
  /* HTTP version will be considered as "HTTP/1.0". */

  /* Parsing data of the request-uri. */
  char hostname[MAXLINE];
  char path[MAXLINE];
  int port;

  /* Hashes of hostname and path. */
  request_line_hash hash_info;
} request_line;


/*
 * RFC 1945 Request-Header.
 * field-name ":" SP field-value CRLF
 */
typedef struct request_header {
  char name[MAXLINE];
  char value[MAXLINE];

  struct request_header *nxt;
} request_header;


/* Response line and headers. */
typedef struct response_header {
  char data[MAXLINE];

  struct response_header *nxt;
} response_header;


/* Block data of an object. */
typedef struct object_block {
  char data[MAXLINE];
  ssize_t len;

  struct object_block *nxt;
} object_block;


/* Cache for response. */
typedef struct response_cache {
  /* Response headers. */
  response_header *hdr;

  /* Object blocks; may be nullptr. */
  object_block *obj;

  /* Total allocated bytes. */
  int totsz;
} response_cache;


/* Cache entry structure. */
typedef struct cache_entry {
  /* Request line. */
  request_line *req_line;

  /* Response cache data. */
  response_cache *cache;
  
  /* Circular linked-list pointers. */
  struct cache_entry *prv, *nxt;
} cache_entry;


/*
 * Semaphores for accessing the cache.
 * Ref: 3rd readers-writers problem.
 */
struct {
  sem_t sem_in;
  sem_t sem_out;
  sem_t sem_w;

  int incnt, outcnt, wait;
} cache_sems;


/* Global cache. */
struct {
  cache_entry *root;

  int left_bytes;
} cache;


/*
 * Cache-related functions.
 */
void init_cache();
void __reroot_cache(cache_entry* const p);
response_cache* __get_response_cache(const request_line* const req_line);
response_cache* get_response_cache(const request_line* const req_line);
void __erase_cache_entry(cache_entry* const entry);
int __push_response_cache(const request_line* const req_line, const response_cache* const res_cache);
int push_response_cache(const request_line* const orig_req_line, const response_cache* const cache);


/*
 * Service functions.
 */
void* service_thread(void *vargp);
void run_service(const int conn_fd);
int service_response(request_line* const req_line, request_header* req_hdrs_root, rio_t* const client_rio, const int conn_fd);
int respond_naive(rio_t* const server_rio, request_line* const req_line, const int conn_fd);
int respond_with_cache(const response_cache* const cache, const int conn_fd);


/*
 * Service-related helper functions.
 */
void push_request_header(request_header** const root, const char* const name, const char* const value);
int parse_request_line(request_line* const req_line, rio_t* const client_rio);
int parse_request_headers(request_header **root, const request_line* const req_line, rio_t* const client_rio);


/*
 * Memory-related functions.
 */
void free_response_cache(response_cache* const p);
request_line* copy_request_line(const request_line* const p);
response_cache* copy_response_cache(const response_cache* const p);


/*
 * Basic rio functions.
 */
void print_rio(const int fd, const char* const str);
void print_rio_header(const int fd, const char* const name, const char* const value);


/*
 * Hash functions.
 */
uint32_t hash(const void* const data, int len);
uint32_t hash_str(const char* const str);
void hash_request_line(request_line* const req_line);


/*
 * Equality functions.
 */
int strprefixeq(const char* const target, const char* const prefix);
int request_line_eq(const request_line* const a, const request_line* const b);


int main(int argc, char *argv[])
{
  /* The listening port must be given as an argument. */
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <listening port>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  /* Initialize the global cache. */
  init_cache();

  /* Ignore SIGPIPE signal. */
  Signal(SIGPIPE, SIG_IGN);

  /* Open listening fd. */
  const int listen_fd = open_listenfd(argv[1]);
  if (listen_fd < 0) {
    fprintf(stderr, "Failed to open listening fd\n");
    exit(EXIT_FAILURE);
  }

  /* Accept incoming connections. */

  struct sockaddr_storage client_addr;
  socklen_t client_addrlen;
  pthread_t tid;

  int failed_fd = -1;

  while (1) {
    /* Try to resolve failed fd. */
    if (0 <= failed_fd) {
      if (!pthread_create(&tid, NULL, service_thread, (void*)(intptr_t)failed_fd)) {
        /* It succeeded. */
        fprintf(stderr, "Failed fd %d is resolved\n", failed_fd);
        failed_fd = -1;
      } else {
        fprintf(stderr, "Failed to resolve fd %d\n", failed_fd);
        continue;
      }
    }

    /* Accept connection. */
    client_addrlen = sizeof(client_addr);
    const int conn_fd = accept(listen_fd, (SA*)(&client_addr), &client_addrlen);

    if (conn_fd < 0) {
      fprintf(stderr, "Failed to accept incoming connection\n");
      continue;
    }

    /* Do proxy service. */
    if (pthread_create(&tid, NULL, service_thread, (void*)(intptr_t)conn_fd)) {
      /* It failed. */
      fprintf(stderr, "Failed to create a new service thread\n");
      failed_fd = conn_fd;
    }
  }

  return EXIT_SUCCESS;
}


/*******************************
 * Cache-related Functions
 *******************************/

/*
 * init_cache - Initialize the global cache
 *    and related semaphores.
 */
void init_cache()
{
  /* Set semaphores for the cache. */
  cache_sems.incnt = 0;
  cache_sems.outcnt = 0;
  cache_sems.wait = 0;
  Sem_init(&(cache_sems.sem_in), 0, 1);
  Sem_init(&(cache_sems.sem_out), 0, 1);
  Sem_init(&(cache_sems.sem_w), 0, 0);

  /* Set default values for the cache. */
  cache.root = NULL;
  cache.left_bytes = MAX_CACHE_SIZE;
}


/*
 * __reroot_cache - Set the given
 *    cache entry as the root.
 */
void __reroot_cache(cache_entry* const p)
{
  if (!p || !(cache.root))
    return;

  if (p == cache.root)
    return;

  /* Now, the list is not a singleton. */

  /* Pop p. */
  p->prv->nxt = p->nxt;
  p->nxt->prv = p->prv;

  /* Push p. */
  p->prv = cache.root->prv;
  p->nxt = cache.root;
  p->prv->nxt = p;
  p->nxt->prv = p;

  /* Set p as the root. */
  cache.root = p;
}


/*
 * __get_response_cache - Get the duplicated
 *    response cache corresponding
 *    to the given request line.
 *    req_line must be not nullptr.
 */
response_cache* __get_response_cache(const request_line* const req_line)
{
  /* The global cache is empty. */
  if (!(cache.root))
    return NULL;

  /* Traversal the global cache. */

  cache_entry *p = cache.root;

  do {
    /* Check the request line is the same. */
    if (request_line_eq(req_line, p->req_line)) {
      /* LRU eviction policy. */
      __reroot_cache(p);
      return copy_response_cache(p->cache);
    }

    p = p->nxt;
  } while (p && p != cache.root);

  return NULL;
}


/*
 * get_response_cache - Get the duplicated
 *    response cache corresponding
 *    to the given request line.
 */
response_cache* get_response_cache(const request_line* const req_line)
{
  if (!req_line)
    return NULL;

  response_cache *ret;
  access_read_cache((ret = __get_response_cache(req_line)));
  return ret;
}


/*
 * __push_response_cache - Insert a new
 *    cache entry with given arguments.
 *    req_line and res_cache must
 *    not be nullptr and not be freed.
 *    The size of a new cache entry
 *    should be small enough to store.
 *    with freeing some entries if necessary.
 */
int __push_response_cache(const request_line* const req_line, const response_cache* const res_cache)
{
  /* Total needed bytes to store the given cache. */
  const int totsz = GET_CACHE_ALLOC_SIZE(res_cache);

  /* Freeing entries to get enough space. */
  while (cache.root && cache.left_bytes < totsz)
    __erase_cache_entry(cache.root->prv);

  if (!(cache.root))
    cache.left_bytes = MAX_CACHE_SIZE;

  /* Make a new cache entry. */
  cache_entry* const entry = malloc(sizeof(cache_entry));
  if (!entry)
    return -1;

  /* Set values. */
  entry->req_line = (request_line*  )req_line;
  entry->cache    = (response_cache*)res_cache;

  /* Apply LRU eviction policy. */

  /* Push the entry. */
  if (cache.root) {
    entry->prv = cache.root->prv;
    entry->nxt = cache.root;

    entry->prv->nxt = entry;
    entry->nxt->prv = entry;
  } else {
    /* Singleton case. */
    entry->prv = entry->nxt = entry;
  }

  /* Set the entry as the root. */
  cache.root = entry;

  /* Decrease the left bytes. */
  cache.left_bytes -= totsz;

  return 0;
}


/*
 * push_response_cache - Insert a new
 *    cache entry with given arguments.
 *    cache should not be freed.
 */
int push_response_cache(const request_line* const orig_req_line, const response_cache* const cache)
{
  if (!orig_req_line || !cache || !(cache->hdr))
    return -1;

  /* The size of a new cache entry is too large. */
  if (MAX_CACHE_SIZE < GET_CACHE_ALLOC_SIZE(cache))
    return -1;

  /* Make a duplicated request line for a new cache entry. */
  const request_line* const req_line = copy_request_line(orig_req_line);
  if (!req_line)
    return -1;

  int retval;
  access_write_cache((retval = __push_response_cache(req_line, cache)));
  return retval;
}


/*
 * __erase_cache_entry - Remove and free
 *    the given cache entry.
 *    entry must be a valid cache entry
 *    or nullptr.
 */
void __erase_cache_entry(cache_entry* const entry)
{
  if (!entry)
    return;

  /* Increase the left bytes. */
  cache.left_bytes += GET_CACHE_ALLOC_SIZE(entry->cache);

  /* Free internal pointers. */
  free(entry->req_line);
  entry->req_line = NULL;
  free_response_cache(entry->cache);
  entry->cache = NULL;

  /* Remove the entry from the global cache. */
  if (entry == entry->nxt) {
    /* Singleton case. */
    cache.root = NULL;
    cache.left_bytes = MAX_CACHE_SIZE;
  } else {
    if (entry == cache.root)
      cache.root = entry->nxt;

    entry->prv->nxt = entry->nxt;
    entry->nxt->prv = entry->prv;
  }

  free(entry);
}


/*******************************
 * Service Functions
 *******************************/

/*
 * service_thread - Thread function to service.
 */
void* service_thread(void *vargp)
{
  /* Get the connection fd. */
  const int conn_fd = (int)(intptr_t)vargp;

  if (conn_fd < 0) {
    fprintf(stderr, "Invalid connection fd\n");
    return NULL;
  }

  /* Detach thread. */
  if (pthread_detach(pthread_self())) {
    fprintf(stderr, "Failed to detach\n");
    return NULL;
  }

  /* Do proxy service. */
  run_service(conn_fd);

  if (close(conn_fd) < 0) {
    fprintf(stderr, "Failed to close connection fd\n");
    return NULL;
  }

  return NULL;
}


/*
 * run_service - Do HTTP services for the client
 *    with given connection fd.
 */
void run_service(const int conn_fd)
{
  /* Set the client rio. */
  rio_t client_rio;
  rio_readinitb(&client_rio, conn_fd);

  /* Parse the first request line. */
  request_line req_line;
  if (parse_request_line(&req_line, &client_rio)) {
    fprintf(stderr, "Failed to parse the request line\n");
    return;
  }

  /* Compute the hash values for the request line. */
  hash_request_line(&req_line);

  /* Linked list for the request headers. */
  request_header *req_hdrs_root = NULL;
  
  /* Parse the remaining request headers. */
  if (parse_request_headers(&req_hdrs_root, &req_line, &client_rio)) {
    fprintf(stderr, "Failed to parse request headers\n");
    return;
  }

  /* Service to the client. */
  if (service_response(&req_line, req_hdrs_root, &client_rio, conn_fd)) {
    fprintf(stderr, "Failed to respond\n");
    return;
  }
}


/*
 * service_response - Service to the client.
 *    req_hdrs_root may be nullptr.
 *    The list of req_hdrs_root will be freed.
 */
int service_response(request_line* const req_line, request_header* req_hdrs_root, rio_t* const client_rio, const int conn_fd)
{
  if (!req_line || !client_rio || conn_fd < 0)
    return -1;

  /* Get cache data if exists. */
  response_cache* const cache = get_response_cache(req_line);
  if (cache) {
    /* Do service with the cache. */
    const int retval = respond_with_cache(cache, conn_fd);

    free_response_cache(cache);
    free(cache);

    return retval;
  }

  /* Connect to the server to get the respond. */

  char port[16];
  sprintf(port, "%d", req_line->port);

  const int server_fd = open_clientfd(req_line->hostname, port);
  if (server_fd < 0) {
    fprintf(stderr, "Connection to the server failed\n");
    return -1;
  }

  /* Set the server rio. */
  rio_t server_rio;
  rio_readinitb(&server_rio, server_fd);

  /* Send a request line. */
  print_rio(server_fd, "GET ");
  print_rio(server_fd, req_line->path);
  print_rio(server_fd, " HTTP/1.0");
  print_rio(server_fd, CRLF);

  /* Send request headers. */
  print_rio_header(server_fd, HDR_USR_AGT,  HDR_DATA_USR_AGT);
  print_rio_header(server_fd, HDR_CONN,     HDR_DATA_CLOSE);
  print_rio_header(server_fd, HDR_PRX_CONN, HDR_DATA_CLOSE);
  for (request_header *p = req_hdrs_root; p;) {
    print_rio_header(server_fd, p->name, p->value);
    void *fp = p;
    p = p->nxt;
    free(fp);
  }
  print_rio(server_fd, CRLF);
  req_hdrs_root = NULL;

  const int retval = respond_naive(&server_rio, req_line, conn_fd);

  if (close(server_fd) < 0) {
    fprintf(stderr, "Failed to close server fd\n");
    return -1;
  }

  return retval;
}


/*
 * respond_naive - Respond to the client
 *    by just serving the responce from the server
 *    and caching it if possible.
 */
int respond_naive(rio_t* const server_rio, request_line* const req_line, const int conn_fd)
{
  if (!server_rio || !req_line || conn_fd < 0)
    return -1;

  response_cache *cache = malloc(sizeof(response_cache));

  if (cache) {
    cache->hdr = NULL;
    cache->obj = NULL;
    cache->totsz = sizeof(response_cache);
  }

  char line[MAXLINE];
  ssize_t len;

  /* Read response line and headers. */

  response_header *hdr_p = NULL;

  while (0 < (len = Rio_readlineb(server_rio, line, MAXLINE))) {
    if (len < 2 || strcmp(line + len - 2, CRLF)) {
      fprintf(stderr, "Response header is too long or not ended with CRLF\n");
      return -1;
    }

    /* Just concat to the client. */
    Rio_writen(conn_fd, line, len);

    if (2 == len)
      break;

    if (!cache)
      continue;

    response_header* const hdr = malloc(sizeof(response_header));

    if (!hdr) {
      free_response_cache(cache);
      free(cache);
      cache = NULL;
      continue;
    }

    strcpy(hdr->data, line);
    hdr->nxt = NULL;

    if (cache->hdr)
      hdr_p->nxt = hdr;
    else
      cache->hdr = hdr;

    hdr_p = hdr;

    cache->totsz += sizeof(response_header);
  }

  /* Read the response object. */

  object_block *obj_p = NULL;
  ssize_t obj_totlen = 0;

  while (0 < (len = Rio_readnb(server_rio, line, MAXLINE))) {
    /* Just concat to the client. */
    Rio_writen(conn_fd, line, len);

    if (!cache)
      continue;

    /* Update total length of the object. */
    obj_totlen += len;

    if (MAX_OBJECT_SIZE < obj_totlen) {
      free_response_cache(cache);
      free(cache);
      cache = NULL;
      continue;
    }

    object_block* const obj = malloc(sizeof(object_block));

    if (!obj) {
      free_response_cache(cache);
      free(cache);
      cache = NULL;
      continue;
    }

    memcpy(obj->data, line, len * sizeof(char));
    obj->len = len;
    obj->nxt = NULL;

    if (cache->obj)
      obj_p->nxt = obj;
    else
      cache->obj = obj;

    obj_p = obj;

    cache->totsz += sizeof(object_block);
  }

  /* If possible to cache, then should not free the cache. */
  if (cache && !push_response_cache(req_line, cache))
    return 0;

  /* Free the (useless) cache. */
  if (cache) {
    free_response_cache(cache);
    free(cache);
    cache = NULL;
  }

  return 0;
}


/*
 * respond_with_cache - Respond to the client
 *    by using the information of the given cache.
 */
int respond_with_cache(const response_cache* const cache, const int conn_fd)
{
  if (!cache || conn_fd < 0)
    return -1;

  /* Send the headers. */
  const response_header *hdr = cache->hdr;
  while (hdr) {
    print_rio(conn_fd, hdr->data);
    hdr = hdr->nxt;
  }
  print_rio(conn_fd, CRLF);

  /* Send the object. */
  const object_block *obj = cache->obj;
  while (obj) {
    Rio_writen(conn_fd, (void*)(obj->data), obj->len);
    obj = obj->nxt;
  }

  return 0;
}


/*******************************
 * Service Helper Functions
 *******************************/

/*
 * push_request_header - Push a new header
 *    with (name, value) into the linked list
 *    starts with root.
 *    name and value must be null-terminated.
 */
void push_request_header(request_header** const root, const char* const name, const char* const value)
{
  if (!root || !name || !value)
    return;

  request_header* const hdr = malloc(sizeof(request_header));
  if (!hdr)
    return;

  strcpy(hdr->name,   name);
  strcpy(hdr->value,  value);
  hdr->nxt = *root;

  *root = hdr;
}


/*
 * parse_request_line - Parse the request line.
 */
int parse_request_line(request_line* const req_line, rio_t* const client_rio)
{
  if (!req_line || !client_rio)
    return -1;

  char line[MAXLINE];
  ssize_t len = Rio_readlineb(client_rio, line, MAXLINE);

  /* Read the very first request line. */
  if (len < 2 || strcmp(line + len - 2, CRLF)) {
    fprintf(stderr, "Request is too long or not ended with CRLF\n");
    return -1;
  }

  /* Strip the line. */
  line[len-2] = line[len-1] = '\0';
  len -= 2;

  /* Check the method is "GET". */
  if (!strprefixeq(line, "GET ")) {
    fprintf(stderr, "Unsupported method\n");
    return -1;
  }

  /* Parse the request-uri. */
  char req_uri[MAXLINE];
  sscanf(line + 4, " %s", req_uri);

  /*
   * HTTP URL
   * "http:" "//" host [ ":" port ] [ absolute_path ]
   */

  /* Assume the absolute path. */
  if (!strprefixeq(req_uri, "http://")) {
    fprintf(stderr, "Request URI must be a valid http URL\n");
    return -1;
  }

  /* Parse the host. */

  char* const host_sp = req_uri + 7;

  char *bufp = host_sp;
  char *hostp = req_line->hostname;

  while (1) {
    const char c = *bufp;
    if ('\0' == c || ':' == c || '/' == c)
      break;

    *hostp = c;
    hostp++;
    bufp++;
  }

  *hostp = '\0';

  /* Check the hostname is non-empty. */
  if ('\0' == *(req_line->hostname)) {
    fprintf(stderr, "Empty hostname\n");
    return -1;
  }

  /* Set the default port as 80. */
  req_line->port = 80;

  /* Port is specified. */
  if (':' == *bufp) {
    int p = -1;

    bufp++;
    while (p <= MAX_PORT) {
      const char c = *bufp;
      if (c < '0' || '9' < c)
        break;

      if (p < 0)
        p = 0;

      p = p*10 + (c&15);
      bufp++;
    }

    if (p < 0 || MAX_PORT < p) {
      fprintf(stderr, "Invalid port\n");
      return -1;
    }

    req_line->port = p;
  }

  /* The remaining part is the path. */
  strcpy(req_line->path, bufp);

  /* Check the path starts with '/'. */

  const char path_sc = *(req_line->path);

  if (path_sc) {
    if ('/' != path_sc) {
      fprintf(stderr, "Absolute path must start with '/'\n");
      return -1;
    }
  } else {
    strcpy(req_line->path, "/");
  }

  return 0;
}


/*
 * parse_request_headers - Parse each request header.
 */
int parse_request_headers(request_header **root, const request_line* const req_line, rio_t* const client_rio)
{
  if (!root || !req_line || !client_rio)
    return -1;

  *root = NULL;

  /* Name and value of the header field. */
  char name[MAXLINE], value[MAXLINE];

  char line[MAXLINE];
  ssize_t len;

  /* Is the host field given? */
  int host_flag = 0;

  while (0 < (len = Rio_readlineb(client_rio, line, MAXLINE))) {
    if (!strcmp(line, CRLF))
      break;

    if (len < 2 || strcmp(line + len - 2, CRLF)) {
      fprintf(stderr, "Request header is too long or not ended with CRLF\n");
      return -1;
    }

    /* Strip the line. */
    line[len-2] = line[len-1] = '\0';
    len -= 2;

    char* const mid_p = strstr(line, CLNSP);
    if (!mid_p) {
      fprintf(stderr, "Invalid request header\n");
      return -1;
    }

    /* Parse name and value. */
    *mid_p = '\0';
    strcpy(name, line);
    strcpy(value, mid_p + 2);

    if ('\0' == name[0]) {
      fprintf(stderr, "Field name of the request header must be non-empty\n");
      return -1;
    }

    if (!strcasecmp(name, HDR_USR_AGT)
     || !strcasecmp(name, HDR_CONN)
     || !strcasecmp(name, HDR_PRX_CONN))
        continue;

    if (!strcasecmp(name, HDR_HOST))
      host_flag = 1;

    push_request_header(root, name, value);
  }

  if (!host_flag)
    push_request_header(root, HDR_HOST, req_line->hostname);

  return 0;
}


/*******************************
 * Memory-related Functions
 *******************************/

/*
 * free_response_cache - Free internal
 *    pointers of the given response cache.
 */
void free_response_cache(response_cache* const p)
{
  if (!p)
    return;

  free_list(p->hdr);
  free_list(p->obj);
}


/*
 * copy_request_line - Return a duplicated request line.
 */
request_line* copy_request_line(const request_line* const p)
{
  if (!p)
    return NULL;

  request_line *ret = malloc(sizeof(request_line));
  if (!ret)
    return NULL;

  memcpy(ret, p, sizeof(request_line));
  return ret;
}


/*
 * copy_response_cache - Return a duplicated response cache.
 */
response_cache* copy_response_cache(const response_cache* const p)
{
  if (!p)
    return NULL;

  response_cache *ret = malloc(sizeof(response_cache));
  if (!ret)
    return NULL;

  copy_list(ret->hdr, p->hdr);
  if (p->hdr && !(ret->hdr)) {
    free(ret);
    return NULL;
  }

  copy_list(ret->obj, p->obj);
  if (p->obj && !(ret->obj)) {
    free_list(ret->hdr);
    free(ret);
    return NULL;
  }

  ret->totsz = p->totsz;

  return ret;
}


/*******************************
 * Basic RIO Functions
 *******************************/

/*
 * print_rio - Print the whole of null-terminated string str.
 */
void print_rio(const int fd, const char* const str)
{
  if (fd < 0 || !str)
    return;

  Rio_writen(fd, (void*)str, strlen(str));
}


/*
 * print_rio_header - Print a HTTP header in one line.
 */
void print_rio_header(const int fd, const char* const name, const char* const value)
{
  if (fd < 0 || !name)
    return;

  print_rio(fd, name);
  print_rio(fd, CLNSP);
  print_rio(fd, value);
  print_rio(fd, CRLF);
}


/*******************************
 * Hash Functions
 *******************************/

/*
 * hash - Fowler-Noll-Vo hash.
 */
uint32_t hash(const void* const data, int len)
{
  if (!data || len < 1)
    return 0;

  const uint8_t *p = (const uint8_t*)data;
  uint32_t r = 0x811c9dc5;
  while (0 < len) {
    r ^= *p;
    r *= 0x1000193;
    p++;
    len--;
  }
  return r;
}


/*
 * hash_str - Hash the given null-terminated string.
 */
uint32_t hash_str(const char* const str)
{
  if (!str)
    return 0;

  return hash(str, strlen(str));
}


/*
 * hash_request_line - Compute hash of internal values.
 */
void hash_request_line(request_line* const req_line)
{
  if (!req_line)
    return;

  req_line->hash_info.hostname  = hash_str(req_line->hostname);
  req_line->hash_info.path      = hash_str(req_line->path);
}


/*******************************
 * Equality Functions
 *******************************/

/*
 * strprefixeq - Return whether the prefix of target
 *    is equivalent to the given prefix.
 *    prefix must be null-terminated.
 */
int strprefixeq(const char* const target, const char* const prefix)
{
  if (!prefix)
    return 1;
  if (!target)
    return 0;

  for (int i = 0; prefix[i]; i++)
    if (target[i] != prefix[i])
      return 0;

  return 1;
}


/*
 * request_line_eq - Return whether
 *    the two given request lines are
 *    equivalent or not.
 */
int request_line_eq(const request_line* const a, const request_line* const b)
{
  if (a == b)
    return 1;
  if (!a || !b)
    return 0;

  if (a->hash_info.hostname != b->hash_info.hostname)
    return 0;
  if(a->hash_info.path != b->hash_info.path)
    return 0;
  if(a->port != b->port)
    return 0;

  if (strcmp(a->hostname, b->hostname))
    return 0;
  if (strcmp(a->path, b->path))
    return 0;

  return 1;
}
