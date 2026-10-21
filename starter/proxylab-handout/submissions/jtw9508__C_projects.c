// #include <stdio.h>
// #include "csapp.h"
// #include <signal.h>
// #include <unistd.h>
// #include <pthread.h>

// /* Recommended max cache and object sizes */
// #define MAX_CACHE_SIZE 1049000
// #define MAX_OBJECT_SIZE 102400

// pthread_mutex_t mutex;

// /* You won't lose style points for including this long line in your code */
// static const char *user_agent_hdr =
//     "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
//     "Firefox/10.0.3\r\n";

// typedef struct cache_node {
//   char *url;
//   char *content;
//   int size;
//   struct cache_node *next, *prev;  
// } cache_node;

// typedef struct cache_list{
//   cache_node *head;
//   cache_node *tail;
//   int total_size;
//   int max_cache_size;
//   int max_block_size;  
// } cache_list;

// typedef struct {
//     int *connfdp;       
//     cache_list *cache_list;  
// } thread_args;

// void doit(int fd, cache_list *cache);
// int parse_uri(char *uri, char *hostname, char *path, char *port);
// void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
// void read_requesthdrs(rio_t *rp);
// void send_get_request_to_server(int clientfd, char *hostname, char *path, char *method);
// void *thread(void *vargp);
// int find_cache(cache_list *cache, cache_node *head, char *full_url, char *cache_buf);
// cache_list *new_cache_list(int max_cache_size, int max_block_size);
// cache_node *create_cache_node(char *url, char *content);
// void renew_cache(int total_size, char *cache_buf, char *full_url, cache_list *cache);
// void change_cache_head(cache_list *cache, cache_node *found);

// cache_node *create_cache_node(char *url, char *content) {
//   cache_node *p = (cache_node *)calloc(1, sizeof(cache_node));
//   p -> url = url;
//   p -> content = content;
//   p -> size = strlen(content);
//   p -> next = NULL;
//   p -> prev = NULL;
//   return p;
// }

// cache_list *new_cache_list(int max_cache_size, int max_block_size) {
//   cache_list *p = (cache_list *)calloc(1, sizeof(cache_list));
//   p -> head = NULL;
//   p -> tail = NULL;
//   p -> total_size = 0;
//   p -> max_cache_size = max_cache_size;
//   p -> max_block_size = max_block_size;  
//   return p;
// }

// thread_args *create_thread_args(int *connfdp, cache_list *cache_list) {
//   thread_args *args = (thread_args*) Malloc(sizeof(thread_args));
//   args-> connfdp = connfdp;  
//   args-> cache_list = cache_list;
//   return args; 
// }

// int main(int argc, char **argv) {  
//   sigset_t sigset;
//   sigemptyset(&sigset);
//   sigaddset(&sigset, SIGPIPE);
//   pthread_sigmask(0, &sigset, NULL);
//   signal(SIGPIPE, SIG_IGN);
//   signal(SIGCHLD, SIG_IGN);
//   pthread_mutex_init(&mutex,NULL);
//   printf("%s", user_agent_hdr);
//   int listenfd, connfd, *connfdp;
//   char hostname[MAXLINE], port[MAXLINE];
//   socklen_t clientlen;
//   struct sockaddr_storage clientaddr;
//   pthread_t tid;
//   cache_list *cache_list;
//   thread_args *args;
//   /* Check command line args */
//   if (argc != 2) {
//     fprintf(stderr, "usage: %s <port>\n", argv[0]);
//     exit(1);
//   }
//   cache_list = new_cache_list(MAX_CACHE_SIZE, MAX_OBJECT_SIZE);
//   listenfd = Open_listenfd(argv[1]);
//   while (1) {    
//     clientlen = sizeof(clientaddr);
//     connfdp = Malloc(sizeof(int));
//     *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
//     Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
//     args = create_thread_args(connfdp,cache_list);
//     Pthread_create(&tid, NULL, thread, args);                   
//   }
// }

// void *thread(void *vargp)
// {
//   thread_args *args = (thread_args *)vargp;
//   int connfd = *(args->connfdp);
//   cache_list *cache = args->cache_list;
//   Pthread_detach(pthread_self());
//   Free(args->connfdp);
//   Free(args);
//   doit(connfd, cache);
//   Close(connfd);
//   return NULL;
// }

// void doit(int fd, cache_list *cache)
// {
//   int is_static, client_proxy;
//   struct stat sbuf; 
//   char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE], path[MAXLINE], port[MAXLINE], full_url[MAXLINE];
//   char filename[MAXLINE], cgiargs[MAXLINE];
//   rio_t rio, rio_proxy;

//   /* Read request line and headers */
//   Rio_readinitb(&rio, fd);
//   Rio_readlineb(&rio, buf, MAXLINE);
//   printf("Request headers:\n");
//   printf("%s", buf);

//   sscanf(buf, "%s %s %s", method, uri, version);
//   // if (strcasecmp(method, "GET")) {
//   //   clienterror(fd, method, "501", "Not implemented",
//   //   "Proxy does not implement this method");
//   //   return;
//   // }

//   // read_requesthdrs(&rio);

//   /* Parse URI from GET request */
//   is_static = parse_uri(uri, hostname, path, port);
//   /* Make full_url which is the key for cached content */
//   strcpy(full_url, hostname);
//   strcat(full_url, ":");
//   strcat(full_url, port);
//   strcat(full_url, path);
//   cache_node *head;
//   char cache_buf[MAX_OBJECT_SIZE];
//   head = cache->head;
//   /* If request is in cache, return it from cache and close thread */
//   pthread_mutex_lock(&mutex);
//   if (find_cache(cache, head, full_url, cache_buf) == 0) {    
//     Rio_writen(fd, cache_buf, strlen(cache_buf));
//     // printf("%s",cache_buf);
//     // char buf[MAXLINE] = "";  // buf를 빈 문자열로 초기화
//     cache_node *current = cache->head;  // cache의 head부터 시작

//     while (current != NULL) {
//         // URL을 buf에 추가
//         snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "->%s", current->url);
//         current = current->next;  // 다음 노드로 이동
//     }
//     printf("%s", buf);
//     // Rio_writen(fd, buf, strlen(buf));
//     // char buf2[16];  // total_size를 문자열로 저장할 버퍼 (충분한 크기로 선언)
//     // snprintf(buf2, sizeof(buf2), "%d", cache->total_size);  // total_size를 문자열로 변환
//     // Rio_writen(fd, buf2, strlen(buf2));  // 변환한 문자열을 fd에 기록
//     // memset(cache_buf,0,MAX_OBJECT_SIZE);
//     pthread_mutex_unlock(&mutex);
//     return;
//   }
//   pthread_mutex_unlock(&mutex);
//   printf("host:%s path: %s method:%s port:%s\n", hostname, path, method, port);
//   client_proxy = open_clientfd(hostname, port);
//   printf("%d",client_proxy);
//   if (client_proxy < 0) {
//     clienterror(fd, hostname, "502", "Bad Gateway",
//     "Proxy can not connect to the target server");
//     // Close(client_proxy);
//     return;
//   }

//   send_get_request_to_server(client_proxy, hostname, path, method);
//   char response[MAXBUF];
//   ssize_t n;
//   size_t total_size = 0;
  
//   Rio_readinitb(&rio_proxy, client_proxy);
//   //if find_cache return -1, then get from real server
//   while ((n = Rio_readlineb(&rio_proxy, response, MAXLINE)) > 0) {
//       Rio_writen(fd, response, n);  /* send data to client */
//       if (total_size + n <= MAX_OBJECT_SIZE) {
//         memcpy(cache_buf+total_size,response,n); /* store data till MAX_OBJECT_SIZE */
//       }      
//       total_size += n; /* tracking total data received */
//   }

//   /* if total_size < MAX_OBJECT_SIZE, put cache_buf in cache */
//   if (total_size <= MAX_OBJECT_SIZE) {
//     pthread_mutex_lock(&mutex);
//     renew_cache(total_size, cache_buf, full_url, cache);
//     pthread_mutex_unlock(&mutex);
//   }
//   Close(client_proxy);
// }

// /* add data to cache */
// void renew_cache(int total_size, char *cache_buf, char *full_url, cache_list *cache) {        
//     char *content = malloc(strlen(cache_buf)+1);
//     strcpy(content, cache_buf);
//     char *url = malloc(strlen(full_url)+1);
//     strcpy(url, full_url);
//     cache_node *new_cache_node = create_cache_node(url,content);
//     int node_size = new_cache_node->size;
//     /* if cache (list size + now size) is not over MAX_CACHE_SIZE, put it on head */
//     if ((cache -> total_size) + node_size <= MAX_CACHE_SIZE) {
//       if (cache->head == NULL) {
//         cache->head = new_cache_node;
//         cache->tail = new_cache_node;
//       } else {
//         cache->head->prev = new_cache_node;
//         new_cache_node->next = cache->head;
//         cache->head = new_cache_node;        
//       }
//       cache->total_size += node_size;
//     } else { /* if cache_size is overed, remove lru cache until get enough space for new cache */ 
//     while ((cache->total_size) + node_size > MAX_CACHE_SIZE) {
//       cache_node *old_block = cache->tail;
//       printf("%s %s", cache->tail->url, cache->tail->prev->url);
//       cache->total_size -= old_block->size;
//       cache->tail = cache->tail->prev;
//       cache->tail->next = NULL;
//       free(old_block->content);
//       free(old_block->url);
//       free(old_block);
//     }
//     cache->head->prev = new_cache_node;
//     new_cache_node->next = cache->head;
//     cache->head = new_cache_node;
//     cache->total_size += node_size;
//     }    
//   }

// /* find data from cache */
// int find_cache(cache_list *cache, cache_node *head, char *full_url, char *cache_buf) {
//   while (head != NULL){
//     if (strcmp(head->url, full_url) == 0) {
//       strcpy(cache_buf, head->content);
//       /* if found node is not head, change head to found node */
//       if (strcmp(cache->head->url,full_url) != 0) {
//         // pthread_mutex_lock(&mutex);
//         change_cache_head(cache, head);
//         // pthread_mutex_unlock(&mutex);
//       }
//       return 0;
//     } 
//     head = head -> next;
//   }
//   return -1;
// }

// void change_cache_head(cache_list *cache, cache_node *found) 
// {
//   found->prev->next = found->next;
//   if (found->next != NULL){
//     found->next->prev = found->prev; /* if found node is not tail */
//   }
//   else {
//     cache->tail = found->prev; /* if found node is tail, change tail */
//   }        
//   cache->head->prev = found;
//   found->next = cache->head;
//   found->prev = NULL;
//   cache->head = found;
  
// }

// void read_requesthdrs(rio_t *rp)
// {
//   char buf[MAXLINE];
//   Rio_readlineb(rp, buf, MAXLINE);
//   while(strcmp(buf, "\r\n")) { /* header and body is separated by \r\n\r\n */
//     Rio_readlineb(rp, buf, MAXLINE);
//     printf("%s", buf);
//   }
//   return;
// }

// int parse_uri(char *uri, char *hostname, char *path, char *port) {
//     char *hostbegin, *hostend, *pathbegin;
//     char defaultport[] = "80";
//     /* find starting point */
//     hostbegin = strstr(uri, "://");
//     if (hostbegin != NULL) {
//         hostbegin += 3;  // ignore 'http://'
//     } else {
//         hostbegin = uri;
//     }
//     /* find path */
//     pathbegin = strchr(hostbegin, '/');
//     if (pathbegin != NULL) {
//         strcpy(path, pathbegin);  
//     } else {
//         strcpy(path, "/");  /* path = root */
//         pathbegin = hostbegin + strlen(hostbegin);  // pathbegin is end of hostbegin
//     }
//     /* extract host and port */
//     hostend = strchr(hostbegin, ':');
//     if (hostend != NULL) {
//         /* extract port*/
//         strncpy(port, hostend + 1, pathbegin - hostend - 1);
//         port[pathbegin - hostend - 1] = '\0';  /* Null-terminator */        
//         /* copy host */ 
//         strncpy(hostname, hostbegin, hostend - hostbegin);
//         hostname[hostend - hostbegin] = '\0';  /* Null-terminator */
//     } else {
//         strcpy(port, defaultport);  /* if not port use default */        
//         /* copy host */ 
//         strncpy(hostname, hostbegin, pathbegin - hostbegin);
//         hostname[pathbegin - hostbegin] = '\0';  /* Null-terminator */  
//     }
//     return 0;
// }

// void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
// {
//   char buf[MAXLINE], body[MAXBUF];
//   /* Build the HTTP response body */
//   sprintf(body, "<html><title>Proxy Error</title>");
//   sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
//   sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
//   sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
//   sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
//   /* Print the HTTP response */
//   sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
//   Rio_writen(fd, buf, strlen(buf));
//   sprintf(buf, "Content-type: text/html\r\n");
//   Rio_writen(fd, buf, strlen(buf));
//   sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
//   Rio_writen(fd, buf, strlen(buf));
//   Rio_writen(fd, body, strlen(body));
// }

// void send_get_request_to_server(int clientfd, char *hostname, char *path, char *method) {
//     char buf[MAXLINE];
//     // Prepare the GET request message
//     sprintf(buf, "%s %s HTTP/1.0\r\n"
//              "Host: %s\r\n"
//              "Connection: close\r\n"
//              "Proxy-Connection: close\r\n"
//              "%s\r\n"
//              "\r\n", method, path,
//              hostname, user_agent_hdr);
//     // Send the GET request to the server
//     Rio_writen(clientfd, buf, strlen(buf));
// }



#include <stdio.h>
#include "csapp.h"
#include <signal.h>
#include <unistd.h>
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

pthread_mutex_t mutex;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

typedef struct cache_node {
  char *url;
  char *content;
  int size;
  struct cache_node *next, *prev;  
} cache_node;

typedef struct cache_list{
  cache_node *head;
  cache_node *tail;
  int total_size;
  int max_cache_size;
  int max_block_size;  
} cache_list;

typedef struct {
    int *connfdp;       
    cache_list *cache_list;  
} thread_args;

void doit(int fd, cache_list *cache);
int parse_uri(char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void read_requesthdrs(rio_t *rp);
void send_get_request_to_server(int clientfd, char *hostname, char *path, char *method);
void *thread(void *vargp);
int find_cache(cache_list *cache, cache_node *head, char *full_url, char *cache_buf);
cache_list *new_cache_list(int max_cache_size, int max_block_size);
cache_node *create_cache_node(char *url, char *content);
void renew_cache(int total_size, char *cache_buf, char *full_url, cache_list *cache);
void change_cache_head(cache_list *cache, cache_node *found);

cache_node *create_cache_node(char *url, char *content) {
  cache_node *p = (cache_node *)calloc(1, sizeof(cache_node));
  p -> url = url;
  p -> content = content;
  p -> size = strlen(content);
  p -> next = NULL;
  p -> prev = NULL;
  return p;
}

cache_list *new_cache_list(int max_cache_size, int max_block_size) {
  cache_list *p = (cache_list *)calloc(1, sizeof(cache_list));
  p -> head = NULL;
  p -> tail = NULL;
  p -> total_size = 0;
  p -> max_cache_size = max_cache_size;
  p -> max_block_size = max_block_size;  
  return p;
}

thread_args *create_thread_args(int *connfdp, cache_list *cache_list) {
  thread_args *args = (thread_args*) Malloc(sizeof(thread_args));
  args-> connfdp = connfdp;  
  args-> cache_list = cache_list;
  return args; 
}

int main(int argc, char **argv) {  
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGPIPE);
  pthread_sigmask(0, &sigset, NULL);
  signal(SIGPIPE, SIG_IGN);
  signal(SIGCHLD, SIG_IGN);
  pthread_mutex_init(&mutex,NULL);
  printf("%s", user_agent_hdr);
  int listenfd, connfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;
  cache_list *cache_list;
  thread_args *args;
  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  cache_list = new_cache_list(MAX_CACHE_SIZE, MAX_OBJECT_SIZE);
  listenfd = Open_listenfd(argv[1]);
  while (1) {    
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // line:netp:tiny:accept
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    args = create_thread_args(connfdp,cache_list);
    Pthread_create(&tid, NULL, thread, args);                   
  }
}

void *thread(void *vargp)
{
  thread_args *args = (thread_args *)vargp;
  int connfd = *(args->connfdp);
  cache_list *cache = args->cache_list;
  Pthread_detach(pthread_self());
  Free(args->connfdp);
  Free(args);
  doit(connfd, cache);
  Close(connfd);
  return NULL;
}

void doit(int fd, cache_list *cache)
{
  int is_static, client_proxy;
  struct stat sbuf; 
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE], path[MAXLINE], port[MAXLINE], full_url[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio, rio_proxy;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("Request headers:\n");
  printf("%s", buf);

  sscanf(buf, "%s %s %s", method, uri, version);
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not implemented",
    "Proxy does not implement this method");
    return;
  }

  read_requesthdrs(&rio);

  /* Parse URI from GET request */
  is_static = parse_uri(uri, hostname, path, port);
  /* Make full_url which is the key for cached content */
  strcpy(full_url, hostname);
  strcat(full_url, ":");
  strcat(full_url, port);
  strcat(full_url, path);
  cache_node *head;
  char cache_buf[MAX_OBJECT_SIZE];
  head = cache->head;
  /* If request is in cache, return it from cache and close thread */
  pthread_mutex_lock(&mutex);
  if (find_cache(cache, head, full_url, cache_buf) == 0) {    
    Rio_writen(fd, cache_buf, strlen(cache_buf));
    pthread_mutex_unlock(&mutex);
    return;
  }
  pthread_mutex_unlock(&mutex);

  printf("host:%s path: %s method:%s port:%s\n", hostname, path, method, port);
  client_proxy = open_clientfd(hostname, port);
  printf("%d",client_proxy);
  if (client_proxy < 0) {
    clienterror(fd, hostname, "502", "Bad Gateway",
    "Proxy can not connect to the target server");
    // Close(client_proxy);
    return;
  }

  send_get_request_to_server(client_proxy, hostname, path, method);
  char response[MAXBUF];
  ssize_t n;
  size_t total_size = 0;
  
  Rio_readinitb(&rio_proxy, client_proxy);
  //if find_cache return -1, then get from real server
  while ((n = Rio_readlineb(&rio_proxy, response, MAXLINE)) > 0) {
      Rio_writen(fd, response, n);  /* send data to client */
      if (total_size + n <= MAX_OBJECT_SIZE) {
        memcpy(cache_buf+total_size,response,n); /* store data till MAX_OBJECT_SIZE */
      }      
      total_size += n; /* tracking total data received */
  }

  /* if total_size < MAX_OBJECT_SIZE, put cache_buf in cache */
  if (total_size <= MAX_OBJECT_SIZE) {
    pthread_mutex_lock(&mutex);
    renew_cache(total_size, cache_buf, full_url, cache);
    pthread_mutex_unlock(&mutex);
  }
  Close(client_proxy);
}

/* add data to cache */
void renew_cache(int total_size, char *cache_buf, char *full_url, cache_list *cache) {        
    char *content = malloc(strlen(cache_buf)+1);
    strcpy(content, cache_buf);
    char *url = malloc(strlen(full_url)+1);
    strcpy(url, full_url);
    cache_node *new_cache_node = create_cache_node(url,content);
    int node_size = new_cache_node->size;
    /* if cache (list size + now size) is not over MAX_CACHE_SIZE, put it on head */
    if ((cache -> total_size) + node_size <= MAX_CACHE_SIZE) {
      if (cache->head == NULL) {
        cache->head = new_cache_node;
        cache->tail = new_cache_node;
      } else {
        cache->head->prev = new_cache_node;
        new_cache_node->next = cache->head;
        cache->head = new_cache_node;        
      }
      cache->total_size += node_size;
    } else { /* if cache_size is overed, remove lru cache until get enough space for new cache */ 
    while ((cache->total_size) + node_size > MAX_CACHE_SIZE) {
      cache_node *old_block = cache->tail;
      printf("%s %s", cache->tail->url, cache->tail->prev->url);
      cache->total_size -= old_block->size;
      cache->tail = cache->tail->prev;
      cache->tail->next = NULL;
      free(old_block->content);
      free(old_block->url);
      free(old_block);
    }
    cache->head->prev = new_cache_node;
    new_cache_node->next = cache->head;
    cache->head = new_cache_node;
    cache->total_size += node_size;
    }    
}

/* find data from cache */
int find_cache(cache_list *cache, cache_node *head, char *full_url, char *cache_buf) {
  while (head != NULL){
    if (strcmp(head->url, full_url) == 0) {
      strcpy(cache_buf, head->content);
      /* if found node is not head, change head to found node */
      if (strcmp(cache->head->url,full_url) != 0) {
        // pthread_mutex_lock(&mutex);
        change_cache_head(cache, head);
        // pthread_mutex_unlock(&mutex);
      }
      return 0;
    } 
    head = head -> next;
  }
  return -1;
}

void change_cache_head(cache_list *cache, cache_node *found) 
{
  found->prev->next = found->next;
  if (found->next != NULL){
    found->next->prev = found->prev; /* if found node is not tail */
  }
  else {
    cache->tail = found->prev; /* if found node is tail, change tail */
  }        
  cache->head->prev = found;
  found->next = cache->head;
  found->prev = NULL;
  cache->head = found;  
}

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) { /* header and body is separated by \r\n\r\n */
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *hostname, char *path, char *port) {
    char *hostbegin, *hostend, *pathbegin;
    char defaultport[] = "80";
    /* find starting point */
    hostbegin = strstr(uri, "://");
    if (hostbegin != NULL) {
        hostbegin += 3;  // ignore 'http://'
    } else {
        hostbegin = uri;
    }
    /* find path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin != NULL) {
        strcpy(path, pathbegin);  
    } else {
        strcpy(path, "/");  /* path = root */
        pathbegin = hostbegin + strlen(hostbegin);  // pathbegin is end of hostbegin
    }
    /* extract host and port */
    hostend = strchr(hostbegin, ':');
    if (hostend != NULL) {
        /* extract port*/
        strncpy(port, hostend + 1, pathbegin - hostend - 1);
        port[pathbegin - hostend - 1] = '\0';  /* Null-terminator */        
        /* copy host */ 
        strncpy(hostname, hostbegin, hostend - hostbegin);
        hostname[hostend - hostbegin] = '\0';  /* Null-terminator */
    } else {
        strcpy(port, defaultport);  /* if not port use default */        
        /* copy host */ 
        strncpy(hostname, hostbegin, pathbegin - hostbegin);
        hostname[pathbegin - hostbegin] = '\0';  /* Null-terminator */  
    }
    return 0;
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];
  /* Build the HTTP response body */
  sprintf(body, "<html><title>Proxy Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
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

void send_get_request_to_server(int clientfd, char *hostname, char *path, char *method) {
    char buf[MAXLINE];
    // Prepare the GET request message
    sprintf(buf, "%s %s HTTP/1.0\r\n"
             "Host: %s\r\n"
             "Connection: close\r\n"
             "Proxy-Connection: close\r\n"
             "%s\r\n"
             "\r\n", method, path,
             hostname, user_agent_hdr);
    // Send the GET request to the server
    Rio_writen(clientfd, buf, strlen(buf));
}


