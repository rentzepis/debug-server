/*
 * proxy.c
 *
 * created by nopeless
 */
#include <stdio.h>
#include <string.h>
#include "csapp.h"
#include "bigboi.h"
#include "safe_queue.h"
#include "url_blacklist.h"

/*
                                              _            __  _
  _________  ____ ___  ____ ___  __  ______  (_)________ _/ /_(_)___  ____
 / ___/ __ \/ __ `__ \/ __ `__ \/ / / / __ \/ / ___/ __ `/ __/ / __ \/ __ \
/ /__/ /_/ / / / / / / / / / / / /_/ / / / / / /__/ /_/ / /_/ / /_/ / / / /
\___/\____/_/ /_/ /_/_/ /_/ /_/\__,_/_/ /_/_/\___/\__,_/\__/_/\____/_/ /_/

*/

/**
 * Enqueued by main thread
 * Dequeued by worker threads
 */
typedef struct ConnectionQueueItem
{
  int connfd;
  struct sockaddr_storage clientaddr;
} ConnectionQueueItem;

void ConnectionQueueItem_init(ConnectionQueueItem *item, int connfd, struct sockaddr_storage clientaddr)
{
  item->connfd = connfd;
  item->clientaddr = clientaddr;
}

void ConnectionQueueItem_free(ConnectionQueueItem *item)
{
  //
}

/**
 * Dynamically spawned by main thread
 */
typedef struct WorkerThreadArg
{
  pthread_t thread_id;
  /**
   * Unique id for thread
   */
  unsigned int uuid;
  /**
   * The index it is allocating i.e. nth active worker thread
   */
  unsigned int idx;
  /**
   * Whether thread is busy
   */
  int busy;
  /**
   * Client connection file descriptor
   */
  SafeQueue *connection_items;
  /**
   * Push here
   */
  SafeQueue *log_items;
  /**
   * Blaklist to use
   */
  UrlBlacklist *blacklist;
} WorkerThreadArg;

/**
 * Enqueued by worker threads
 *  Dequeued by logger threads
 */
typedef struct LogQueueItem
{
  char *message;
  struct timespec *timestamp;
  /**
   * optional fields
   */
  struct sockaddr_storage sockaddr;
  char *uri;
  int content_size;
} LogQueueItem;

struct sockaddr_storage SOCKADDR_EMPTY = {0};

/**
 * Everything is optional
 */
void LogQueueItem_init(
    LogQueueItem *item,
    char *message,
    struct sockaddr_storage sockaddr,
    char *uri,
    int content_size)
{
  struct timespec *ts = malloc(sizeof(*ts));
  clock_gettime(CLOCK_REALTIME, ts);

  item->message = message;
  item->timestamp = ts;

  item->sockaddr = sockaddr;
  item->uri = uri;
  item->content_size = content_size;
}

void LogQueueItem_free(LogQueueItem *item)
{
  if (item->message)
    free(item->message);
  if (item->uri)
    free(item->uri);
  free(item->timestamp);
}

/**
 * Static number of logging threads
 */
typedef struct LoggerThreadArg
{
  pthread_t thread_id;
  unsigned int idx;
  /**
   * Pop here
   */
  SafeQueue *log_items;
  /**
   * Push here
   */
  SafeQueue *file_write_items;
} LoggerThreadArg;

/**
 * Enqueued by logger threads and main thread
 * Dequeued by file writer threads
 */
typedef struct FileWriteItem
{
  char *content;
  /**
   * Can be 0
   *
   * if 0 use strlen
   */
  unsigned int length;
} FileWriteItem;

void FileWriteItem_init(FileWriteItem *item, char *content, unsigned int length)
{
  item->content = content;
  item->length = length;
}

void FileWriteItem_free(FileWriteItem *item)
{
  free(item->content);
}

/**
 * One file writer thread gets spawned by main
 */
typedef struct FileWriterArg
{
  SafeQueue *sq;
  int file_fd;
} FileWriterArg;

/*
         __  _ __
  __  __/ /_(_) /
 / / / / __/ / /
/ /_/ / /_/ / /
\__,_/\__/_/_/

UTIL
*/

char *strlwr(char *str)
{
  for (char *p = str; *p; p++)
    *p = tolower(*p);

  return str;
}

char *strmalloccpy(const char *src)
{
  char *dst = malloc(strlen(src) + 1);
  strcpy(dst, src);
  return dst;
}

char *new_html(char *title, char *body)
{
  int html_len = strlen(title) + strlen(body) + 1024;
  char *html = malloc(html_len);
  snprintf(html, html_len, "<!DOCTYPE html>\n<html>\n<head>\n<title>\n%s\n</title>\n</head>\n<body>\n%s\n</body>\n</html>\n", title, body);
  return html;
}

char *new_http_html_response(char *html)
{
  int res_len = strlen(html) + 1024;
  char *response = malloc(res_len);
  snprintf(response, res_len, "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", strlen(html), html);
  return response;
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
  char *hostbegin;
  char *hostend;
  char *pathbegin;
  int len;

  if (strncasecmp(uri, "http://", 7) != 0)
  {
    hostname[0] = '\0';
    return -1;
  }

  /* Extract the host name */
  hostbegin = uri + 7;
  hostend = strpbrk(hostbegin, " :/\r\n\0");
  len = hostend - hostbegin;
  strncpy(hostname, hostbegin, len);
  hostname[len] = '\0';

  /* Extract the port number */
  *port = 80; /* default */
  if (*hostend == ':')
    *port = atoi(hostend + 1);

  /* Extract the path */
  pathbegin = strchr(hostbegin, '/');
  if (pathbegin == NULL)
  {
    pathname[0] = '\0';
  }
  else
  {
    strcpy(pathname, pathbegin);
  }

  return 0;
}

/**
 * Free the BigBoi object returned by this function
 */
BigBoi *log_entry(
    BigBoi *bb,
    char *logstring,
    struct sockaddr_storage sockaddr,
    char *uri,
    int size)
{
  if (!bb)
    return NULL;

  char time_str[32];
  unsigned long host;

  time_t now = time(NULL);
  strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

  // [Date] browserIP URL size
  // [Sun 27 Oct 2002 02:51:02 EST] 128.2.111.38 https://www.cs.cmu.edu/ 34314

  BigBoi_append_str(bb, "[");
  BigBoi_append_str(bb, time_str);
  BigBoi_append_str(bb, "]");

  if (sockaddr.ss_family)
  {
    host = (*(struct sockaddr_in *)&sockaddr).sin_addr.s_addr;
    unsigned char a, b, c, d;
    a = host & 0xFF;
    b = (host >> 8) & 0xFF;
    c = (host >> 16) & 0xFF;
    d = (host >> 24) & 0xFF;

    char ip[18];
    sprintf(ip, " %d.%d.%d.%d", a, b, c, d);
    BigBoi_append_str(bb, ip);
  }

  if (uri)
  {
    BigBoi_append_str(bb, " ");
    BigBoi_append_str(bb, uri);
  }

  BigBoi_append_str(bb, " ");

  if (!logstring)
    return bb;

  if (size > 0)
  {
    char size_str[16];
    sprintf(size_str, "%d", size);
    BigBoi_append_str(bb, "[payload size: ");
    BigBoi_append_str(bb, size_str);
    BigBoi_append_str(bb, "] ");
  }

  char *next_newline = strchr(logstring, '\n');
  if (!next_newline)
  {
    BigBoi_append_str(bb, logstring);
    BigBoi_append_str(bb, "\n");
    return bb;
  }

  for (
      ; logstring[0];
      logstring = next_newline + 1,
      next_newline = strchr(logstring, '\n'))
  {

    BigBoi_append_str(bb, "\n -  ");
    if (!next_newline)
    {
      BigBoi_append_str(bb, logstring);
      break;
    }
    BigBoi_append_strn(bb, logstring, next_newline - logstring);
  }

  BigBoi_append_str(bb, "\n");
  return bb;
}

typedef struct CliArgs
{
  unsigned int port_num;
  char *port_str;
} CliArgs;

void parse_args(CliArgs *cli_args, int argc, char **argv)
{
  char *argv_port;

  /* Check arguments */
  if (argc != 2)
  {
    // fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
    // exit(0);
    argv_port = "26180";
  }
  else
  {
    argv_port = argv[1];
  }

  cli_args->port_num = atoi(argv_port);
  cli_args->port_str = argv_port;
}

/*
       __     _____       _ __  _
  ____/ /__  / __(_)___  (_) /_(_)___  ____  _____
 / __  / _ \/ /_/ / __ \/ / __/ / __ \/ __ \/ ___/
/ /_/ /  __/ __/ / / / / / /_/ / /_/ / / / (__  )
\__,_/\___/_/ /_/_/ /_/_/\__/_/\____/_/ /_/____/

*/
#define MAX_WORKER_THREADS 100
#define LOGGER_THREADS 5
#define QUEUE_SIZE 1024

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/*
                      __                __  __                        __
 _      ______  _____/ /_____  _____   / /_/ /_  ________  ____ _____/ /
| | /| / / __ \/ ___/ //_/ _ \/ ___/  / __/ __ \/ ___/ _ \/ __ `/ __  /
| |/ |/ / /_/ / /  / ,< /  __/ /     / /_/ / / / /  /  __/ /_/ / /_/ /
|__/|__/\____/_/  /_/|_|\___/_/      \__/_/ /_/_/   \___/\__,_/\__,_/

*/

void *worker_thread(WorkerThreadArg *arg)
{
  signal(SIGPIPE, SIG_IGN);

  SafeQueue *connection_sq = arg->connection_items;
  SafeQueue *log_sq = arg->log_items;

  LogQueueItem *log_item = malloc(sizeof(*log_item));
  char *message;
  asprintf(&message, "Worker %d in worker queue %d created", arg->uuid, arg->idx);
  LogQueueItem_init(log_item, message, SOCKADDR_EMPTY, NULL, 0);
  SafeQueue_push(log_sq, log_item);

  BigBoi *bb = BigBoi_new(32);

  while (1)
  {
    arg->busy = 0;
    ConnectionQueueItem *conn_item = SafeQueue_pop(connection_sq);
    arg->busy = 1;

    if (!conn_item)
    {
      break;
    }

    int connfd = conn_item->connfd;
    struct sockaddr_storage clientaddr = conn_item->clientaddr;
    free(conn_item);

    LogQueueItem *log_item = malloc(sizeof(*log_item));
    char *message;
    asprintf(&message, "Got work for %d", connfd);
    LogQueueItem_init(log_item, message, clientaddr, NULL, 0);
    SafeQueue_push(log_sq, log_item);

    // Handle request
    char client_port[MAXLINE];
    char client_hostname[256];
    socklen_t clientlen = sizeof(clientaddr);

    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE,
                client_port, MAXLINE, 0);

    log_item = malloc(sizeof(*log_item));
    asprintf(&message, "Accepted connection from (%s, %s)", client_hostname, client_port);
    LogQueueItem_init(log_item, message, clientaddr, NULL, 0);
    SafeQueue_push(log_sq, log_item);

    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    rio_t rio;
    rio_readinitb(&rio, connfd);
    if (rio_readlineb(&rio, buf, MAXLINE) < 0)
      goto close_fd;

    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcmp(method, "GET"))
    {
      log_item = malloc(sizeof(*log_item));
      asprintf(&message, "Method %s not implemented", method);
      LogQueueItem_init(log_item, message, clientaddr, NULL, 0);
      SafeQueue_push(log_sq, log_item);
      goto close_fd;
    }

    // header
    char header[MAXLINE];
    ssize_t s;
    while ((s = rio_readlineb(&rio, header, MAXLINE)) > 2)
    {
      if (s < 0)
        goto close_fd;
    }

    BigBoi_reset(bb);
    BigBoi_append_str(bb, "Request headers: \n");
    BigBoi_append_str(bb, buf);
    log_item = malloc(sizeof(*log_item));
    LogQueueItem_init(log_item, BigBoi_to_str(bb), clientaddr, NULL, 0);
    SafeQueue_push(log_sq, log_item);

    // uri
    char hostname[256];
    char pathname[MAXLINE];
    int port_num;
    char port_str[6];
    parse_uri(uri, hostname, pathname, &port_num);

    // normalize hostname to lowercase
    strlwr(hostname);

    // check blacklist
    char *rule;
    if ((rule = UrlBlacklist_exists(arg->blacklist, hostname)))
    {
      log_item = malloc(sizeof(*log_item));
      rule = UrlBlacklist_get_rule(arg->blacklist, rule);
      asprintf(&message, "Blacklisted %s for our client %d due to rule: %s", hostname, connfd, rule);
      free(rule);
      LogQueueItem_init(log_item, message, clientaddr, NULL, 0);
      SafeQueue_push(log_sq, log_item);

      asprintf(&message, "%s has been blocked by the proxy server", hostname);
      char *html = new_html("Blocked", message);
      free(message);
      char *http_res = new_http_html_response(html);
      free(html);
      if (rio_writen(connfd, http_res, strlen(http_res)) < 0)
      {
        free(http_res);
        goto close_fd;
      }
      free(http_res);
      goto close_fd;
    }

    sprintf(port_str, "%d", port_num);

    // Open client connection
    int clientfd = open_clientfd(hostname, port_str);

    if (clientfd < 0)
    {
      log_item = malloc(sizeof(*log_item));
      asprintf(&message, "Cannot establish connection to %s for our client %d. Reason: %s", hostname, connfd, strerror(errno));
      LogQueueItem_init(log_item, message, clientaddr, NULL, 0);
      SafeQueue_push(log_sq, log_item);
      goto close_fd;
    }

    log_item = malloc(sizeof(*log_item));
    asprintf(&message, "Established connection to %s for our client %d", hostname, connfd);
    LogQueueItem_init(log_item, message, clientaddr, NULL, 0);
    SafeQueue_push(log_sq, log_item);

    // Forward request to server
    char line[MAXLINE << 1];
    BigBoi_reset(bb);
    snprintf(line, sizeof(line), "GET %s HTTP/1.0\r\n", pathname);
    BigBoi_append_str(bb, line);
    sprintf(line, "Host: %s\r\n", hostname);
    BigBoi_append_str(bb, line);
    BigBoi_append_str(bb, user_agent_hdr);
    BigBoi_append_str(bb, "Connection: close\r\n");
    BigBoi_append_str(bb, "Proxy-Connection: close\r\n\r\n");

    char *str = BigBoi_to_str(bb);
    if (rio_writen(clientfd, str, bb->total_length) < 0)
    {
      free(str);
      goto close_fd;
    }
    free(str);

    // Receive response from server
    rio_t server_rio;
    rio_readinitb(&server_rio, clientfd);

    BigBoi_reset(bb);
    for (int n; (n = rio_readlineb(&server_rio, buf, MAXLINE)) > 0;)
    {
      if (n < 0)
        goto close_fd;

      BigBoi_append_strn(bb, buf, n);
      if (rio_writen(connfd, buf, n) < 0)
      {
        goto close_fd;
      }
    }

    // Log response
    log_item = malloc(sizeof(*log_item));
    asprintf(&message, "sending payload for %d", connfd);
    LogQueueItem_init(log_item, message, clientaddr, strmalloccpy(uri), 0);
    SafeQueue_push(log_sq, log_item);

    log_item = malloc(sizeof(*log_item));
    LogQueueItem_init(log_item, BigBoi_to_str(bb), clientaddr, strmalloccpy(uri), bb->total_length);
    SafeQueue_push(log_sq, log_item);

    Close(connfd);

    log_item = malloc(sizeof(*log_item));
    asprintf(&message, "completed request %d", connfd);
    LogQueueItem_init(log_item, message, clientaddr, strmalloccpy(uri), 0);
    SafeQueue_push(log_sq, log_item);
    continue;

  close_fd:
    Close(connfd);

    log_item = malloc(sizeof(*log_item));
    asprintf(&message, "encountered issues with request %d", connfd);
    LogQueueItem_init(log_item, message, clientaddr, NULL, 0);
    SafeQueue_push(log_sq, log_item);
    continue;
  }

  log_item = malloc(sizeof(*log_item));
  asprintf(&message, "Worker %d in worker queue %d exiting", arg->uuid, arg->idx);
  LogQueueItem_init(log_item, message, SOCKADDR_EMPTY, NULL, 0);
  SafeQueue_push(log_sq, log_item);

  BigBoi_free(bb);

  arg->busy = 0;
  pthread_exit(NULL);
}

/*
    __                               __  __                        __
   / /___  ____ _____ ____  _____   / /_/ /_  ________  ____ _____/ /
  / / __ \/ __ `/ __ `/ _ \/ ___/  / __/ __ \/ ___/ _ \/ __ `/ __  /
 / / /_/ / /_/ / /_/ /  __/ /     / /_/ / / / /  /  __/ /_/ / /_/ /
/_/\____/\__, /\__, /\___/_/      \__/_/ /_/_/   \___/\__,_/\__,_/
        /____//____/

*/

void *logger_thread(LoggerThreadArg *arg)
{
  SafeQueue *log_sq = arg->log_items;
  SafeQueue *file_write_sq = arg->file_write_items;
  BigBoi *bb = BigBoi_new(32);

  char *message;

  FileWriteItem *file_item = malloc(sizeof(*file_item));
  FileWriteItem_init(file_item, message, asprintf(&message, "logger %d started\n", arg->idx));
  SafeQueue_push(file_write_sq, file_item);

  while (1)
  {
    LogQueueItem *item = SafeQueue_pop(log_sq);

    if (!item)
    {
      FileWriteItem *file_item = malloc(sizeof(*file_item));
      FileWriteItem_init(file_item, message, asprintf(&message, "logger %d exited\n", arg->idx));
      SafeQueue_push(file_write_sq, file_item);
      break;
    }

    FileWriteItem *file_item = malloc(sizeof(*file_item));

    BigBoi_reset(bb);
    log_entry(bb, item->message, item->sockaddr, item->uri, item->content_size);
    char *content = BigBoi_to_str(bb);
    FileWriteItem_init(file_item, content, bb->total_length);

    SafeQueue_push(file_write_sq, file_item);

    LogQueueItem_free(item);
    free(item);
  }

  BigBoi_free(bb);
  pthread_exit(NULL);
}

/*
    _____ __                      _ __
   / __(_) /__     _      _______(_) /____  _____
  / /_/ / / _ \   | | /| / / ___/ / __/ _ \/ ___/
 / __/ / /  __/   | |/ |/ / /  / / /_/  __/ /
/_/ /_/_/\___/    |__/|__/_/  /_/\__/\___/_/

*/
void *file_writer_thread(FileWriterArg *arg)
{
  SafeQueue *sq = arg->sq;
  int file_fd = arg->file_fd;

  while (1)
  {
    FileWriteItem *item = SafeQueue_pop(sq);

    if (!item)
    {
      char *end_message = "end of write log\n";
      write(file_fd, end_message, strlen(end_message));
      break;
    }

    if (!item->length)
      item->length = strlen(item->content);
    write(file_fd, item->content, item->length);

    FileWriteItem_free(item);
    free(item);
  }

  pthread_exit(NULL);
}

volatile sig_atomic_t should_close_server = 0;

void sigint_handler(int sig)
{
  if (should_close_server)
  {
    char t[] = "Server force terminated\n";
    write(STDOUT_FILENO, t, sizeof(t));
    exit(0);
  }
  char t[] = "termination request accepted. Closing server...\n";
  write(STDOUT_FILENO, t, sizeof(t));
  should_close_server = 1;
}

/*
                    _          __  __                        __
   ____ ___  ____ _(_)___     / /_/ /_  ________  ____ _____/ /
  / __ `__ \/ __ `/ / __ \   / __/ __ \/ ___/ _ \/ __ `/ __  /
 / / / / / / /_/ / / / / /  / /_/ / / / /  /  __/ /_/ / /_/ /
/_/ /_/ /_/\__,_/_/_/ /_/   \__/_/ /_/_/   \___/\__,_/\__,_/

*/
int main(int argc, char **argv)
{
  signal(SIGINT, sigint_handler);

  CliArgs args;
  parse_args(&args, argc, argv);

  char hostname[256];
  strcpy(hostname, "localhost");

  int listenfd = Open_listenfd(args.port_str);

  fcntl(listenfd, F_SETFL, fcntl(listenfd, F_GETFL, 0) | O_NONBLOCK);

  printf("Proxy server running on port %s\n", args.port_str);

  // initialize blacklist
  UrlBlacklist blacklist;
  UrlBlacklist_new(&blacklist, "blacklist.txt", '\n', 20);
  UrlBlacklist_print_table(&blacklist);

  // initialize queues
  SafeQueue connection_sq = SafeQueue_new(QUEUE_SIZE);
  SafeQueue log_sq = SafeQueue_new(QUEUE_SIZE);
  SafeQueue file_write_sq = SafeQueue_new(QUEUE_SIZE);

  // initialize file writer
  int logfile_fd = Open("proxy.log", O_CREAT | O_WRONLY | O_TRUNC, S_IRUSR | S_IWUSR);
  FileWriterArg file_writer_arg = {&file_write_sq, logfile_fd};
  pthread_t file_writer_pt;
  pthread_create(&file_writer_pt, NULL, (void *(*)(void *))file_writer_thread, &file_writer_arg);

  // initialize logger threads
  LoggerThreadArg logger_args[LOGGER_THREADS] = {0};
  for (int i = 0; i < LOGGER_THREADS; i++)
  {
    logger_args[i] = (LoggerThreadArg){0, i, &log_sq, &file_write_sq};
    pthread_t logger_pt;
    pthread_create(&logger_pt, NULL, (void *(*)(void *))logger_thread, &logger_args[i]);
    logger_args[i].thread_id = logger_pt;
  }

  unsigned int uuid = 0;

  // initialize worker threads
  WorkerThreadArg worker_args[MAX_WORKER_THREADS] = {0};
  worker_args[0] = (WorkerThreadArg){0, uuid++, 0, 0, &connection_sq, &log_sq, &blacklist};
  pthread_t worker_pt;
  pthread_create(&worker_pt, NULL, (void *(*)(void *))worker_thread, &worker_args[0]);
  worker_args[0].thread_id = worker_pt;

  char *log_message;
  asprintf(&log_message, "main thread started\n");
  FileWriteItem *file_item = malloc(sizeof(*file_item));
  FileWriteItem_init(file_item, log_message, strlen(log_message));
  SafeQueue_push(&file_write_sq, file_item);

  for (int i = 0; i < 1000;)
  {
    if (should_close_server || errno == EINTR)
      break;

    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    clientlen = sizeof(clientaddr);

    int connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
    if (errno == EWOULDBLOCK || connfd < 0)
    {
      errno = 0;
      // check every 10 ms
      usleep(10 * 1000);
      continue;
    }

    i++;

    // if all workers busy make more
    char need_more_workers = 1;
    for (int i = 0; i < MAX_WORKER_THREADS; i++)
    {
      if (!worker_args[i].thread_id)
        continue;

      if (worker_args[i].busy == 0)
      {
        need_more_workers = 0;
        break;
      }
    }

    if (need_more_workers)
    {
      for (int i = 0; i < MAX_WORKER_THREADS; i++)
      {
        if (worker_args[i].thread_id)
          continue;

        worker_args[i] = (WorkerThreadArg){0, uuid++, i, 0, &connection_sq, &log_sq, &blacklist};
        pthread_t worker_pt;
        pthread_create(
            &worker_pt,
            NULL,
            (void *(*)(void *))worker_thread,
            &worker_args[i]);
        worker_args[i].thread_id = worker_pt;
        printf("Created worker thread %d\n", i);
        break;
      }
    }

    ConnectionQueueItem *conn_item = malloc(sizeof(*conn_item));
    ConnectionQueueItem_init(conn_item, connfd, clientaddr);
    SafeQueue_push(&connection_sq, conn_item);
  }

  // Clean up
  printf("Waiting for connections to finish\n");
  ConnectionQueueItem **remaining_connections = (ConnectionQueueItem **)SafeQueue_exit(&connection_sq, 5 * 1000000);
  if (remaining_connections)
  {
    printf("Closing remaining connections\n");
    for (int i = 0; remaining_connections[i]; i++)
    {
      Close(remaining_connections[i]->connfd);
    }
    printf("Closed remaining connections\n");
  }
  else
  {
    printf("No remaining connections.\n");
  }

  for (int i = 0; i < MAX_WORKER_THREADS; i++)
  {
    if (worker_args[i].thread_id)
      pthread_join(worker_args[i].thread_id, NULL);
  }

  printf("closing %d loggers\n", LOGGER_THREADS);
  SafeQueue_exit(&log_sq, -1);

  for (int i = 0; i < LOGGER_THREADS; i++)
  {
    pthread_join(logger_args[i].thread_id, NULL);
  }

  printf("closing file writer\n");
  SafeQueue_exit(&file_write_sq, -1);

  pthread_join(file_writer_pt, NULL);

  close(logfile_fd);

  SafeQueue_free(&connection_sq);
  SafeQueue_free(&log_sq);
  SafeQueue_free(&file_write_sq);

  UrlBlacklist_free(&blacklist);

  printf("Proxy server exited\n");

  return 0;
}
