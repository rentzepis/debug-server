#include "proxy.h"
#include "csapp.h"
#include "sbuf.h"

static cache_t caches[MAX_CACHES];
static worker_t workers[MAX_WORKERS];
static sbuf_t sbuf;
static int readpos = 0;
static int readcnt = 0;
static sem_t mutex, w;

static void free_url(url_t *url)
{
  memset(url, 0, sizeof(url_t));
}

static int urlcmp(url_t *url1, url_t *url2)
{
  int n = strcmp(url1->hostname, url2->hostname) ||
          strcmp(url1->port, url2->port) ||
          strcmp(url1->uriargs, url2->uriargs);

  return n;
}

static void urlcopy(url_t *dest, url_t *src)
{
  strcpy(dest->hostname, src->hostname);
  strcpy(dest->port, src->port);
  strcpy(dest->uriargs, src->uriargs);
}

static void init_caches(cache_t *caches)
{
  for (int i = 0; i < MAX_CACHES; i++)
    free_cache(&caches[i]);
  readpos = 0;
}

static void free_cache(cache_t *cache)
{
  free_url(&cache->url);
  memset(cache->buf, 0, sizeof(cache->buf));
  cache->alloc = 0;
}

static cache_t *find_cache(cache_t *caches, url_t *url)
{
  for (int i = 0; i < MAX_CACHES; i++)
  {
    if (urlcmp(&caches[i].url, url) == 0)
    {
      return caches + i;
    }
  }
  return NULL;
}

static void fill_cache(cache_t *caches, url_t *url, char *buf, int alloc)
{
  cache_t *cache;

  P(&w);
  P(&mutex);

  cache = &caches[readpos];
  free_cache(cache);
  readpos = (readpos + 1) % MAX_CACHES;
  urlcopy(&cache->url, url);
  memcpy(cache->buf, buf, alloc);
  cache->alloc = alloc;
  V(&mutex);
  V(&w);
}

static void init_worker_pool()
{
  for (int i = 0; i < MAX_WORKERS; i++)
    free_worker(&workers[i]);
}

static void free_worker(worker_t *worker)
{
  memset(worker->buf, 0, sizeof(worker->buf));
}

static int receive(int connfd, char *buf, rio_t *cio)
{
  Rio_readinitb(cio, connfd);
  return Rio_readlineb(cio, buf, MAXLINE);
}

static int parse_uri(url_t *url, char *buf)
{
  http_t http;

  sscanf(buf, "%s %s %s", http.method, http.url, http.version);

  if (strcasecmp(http.method, "GET"))
  {
    printf("ONLY GET is implemented.\n");
    return -1;
  }
  int uargc = sscanf(http.url, "%*[^:]%*[:/]%[^:]:%[^/]%s", url->hostname, url->port, url->uriargs);
  if (uargc != 3)
  {
    printf("Error: url parsing.");
    return -1;
  }
  return 0;
}

static int forward_cache(url_t *url, rio_t *cio)
{
  cache_t *cache;
  int n = 0;

  P(&mutex);
  readcnt++;
  if (readcnt == 1)
    P(&w);
  V(&mutex);

  if ((cache = find_cache(caches, url)))
  {
    Rio_writen(cio->rio_fd, cache->buf, cache->alloc);
    n = cache->alloc;
  }

  P(&mutex);
  readcnt--;
  if (readcnt == 0)
    V(&w);
  V(&mutex);

  return n;
}

static int forward_request(url_t *url, char *buf, rio_t *cio, rio_t *sio)
{
  int clientfd, alloc = 0, n;

  clientfd = Open_clientfd(url->hostname, url->port);
  Rio_readinitb(sio, clientfd);
  sprintf(buf, "GET %s HTTP/1.0\r\n\r\n", url->uriargs);
  Rio_writen(clientfd, buf, strlen(buf));

  alloc = Rio_readlineb(sio, buf, MAXLINE);
  alloc += sprintf(buf + alloc, "Host: %s:%s\r\n", url->hostname, url->port);
  alloc += sprintf(buf + alloc, "%s", user_agent_hdr);
  alloc += sprintf(buf + alloc, "Connection: close\r\n");
  alloc += sprintf(buf + alloc, "Proxy-Connection: close\r\n");
  Rio_writen(cio->rio_fd, buf, strlen(buf));

  while ((n = Rio_readnb(sio, buf + alloc, MAX_OBJECT_SIZE)) != 0)
  {
    Rio_writen(cio->rio_fd, buf + alloc, n);
    alloc += n;
  }
  fill_cache(caches, url, buf, alloc);
  return clientfd;
}

static void response(int connfd, worker_t *worker)
{
  url_t url;
  int clientid;
  if (receive(connfd, worker->buf, &worker->cio) == 0)
    return;
  if (parse_uri(&url, worker->buf) < 0)
    return;
  if (!forward_cache(&url, &worker->cio))
  {
    clientid = forward_request(&url, worker->buf, &worker->cio, &worker->sio);
    Close(clientid);
  }
}

void *thread(void *vargsp)
{
  worker_t *worker = (worker_t *)vargsp;
  int connfd;

  Pthread_detach(Pthread_self());

  while (1)
  {
    connfd = pop_sbuf(&sbuf);
    response(connfd, worker);
    Close(connfd);
  }
  return NULL;
}

int main(int argc, char *argv[])
{
  int port, listenfd, connfd;
  socklen_t clientlen;
  sas_t clientaddr;
  pthread_t tid;

  if (argc != 2)
  {
    printf("Usage: proxy PORT \n");
    exit(1);
  }

  port = strtol(argv[1], NULL, 10);

  if (port < 1024 || port > 65535)
  {
    printf("Port must be in the range of 1024-65535");
    exit(1);
  }

  init_sbuf(&sbuf);
  init_worker_pool();
  init_caches(caches);
  printf("%ld\n", MAX_CACHES);
  Sem_init(&mutex, 0, 1);
  Sem_init(&w, 0, 1);

  listenfd = Open_listenfd(argv[1]);

  for (int i = 0; i < MAX_WORKERS; i++)
    Pthread_create(&tid, NULL, thread, &workers[i]);

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (sa_t *)&clientaddr, &clientlen);
    push_sbuf(&sbuf, connfd);
  }

  return 0;
}
