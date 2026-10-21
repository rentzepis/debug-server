#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include "cache.h"
#include "csapp.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE  1049000
#define MAX_OBJECT_SIZE 102400

#define MAX(x, y) ((x) < (y) ? (y) : (x))
#define MIN(x, y) ((x) > (y) ? (y) : (x))

static __thread char perrorbuf[1024];

// NOLINTBEGIN(clang-diagnostic-gnu-zero-variadic-macro-arguments)

#define eprintf(loglvl, fmt, ...)                                              \
  fprintf(stderr, #loglvl ": %s(%s:%d): " fmt, __FUNCTION__, __FILE__,         \
          __LINE__, ##__VA_ARGS__)

#define eprintln(loglvl, fmt, ...) eprintf(loglvl, fmt "\n", ##__VA_ARGS__)

#define perrorfl(msg)                                                          \
  do {                                                                         \
    if (strerror_r(errno, perrorbuf, sizeof(perrorbuf)) != 0) {                \
      perror(msg);                                                             \
      break;                                                                   \
    }                                                                          \
    eprintln(error, "%s: %s", (msg), perrorbuf);                               \
  } while (0)

// NOLINTEND(clang-diagnostic-gnu-zero-variadic-macro-arguments)

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
  "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";

static int startswith(const char *str, const char *prefix) {
  return strncmp(str, prefix, strlen(prefix)) == 0;
}

static int startswithcase(const char *str, const char *prefix) {
  return strncasecmp(str, prefix, strlen(prefix)) == 0;
}

static char *resolve_port(int argc, char *argv[]) {
  if (argc == 1)
    return "80";

  if (argc > 2) {
    eprintln(error, "superflous command line arguments\n"
                    "usage: ./proxy [port]");
    exit(1);
  }

  return argv[1];
}

static int sockopt_bind_listen(int fd, const struct sockaddr *addr,
                               socklen_t addrlen) {
  int optval = 1;

  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval,
                 sizeof(optval))
      < 0) {
    return -1;
  }

  if (bind(fd, addr, addrlen) < 0)
    return -1;

  return listen(fd, LISTENQ);
}

static int wrap_socket_open(const char *host, const char *port, int flags,
                            int (*trial)(int, const struct sockaddr *,
                                         socklen_t)) {
  struct addrinfo hint, *result, *ai;
  int ret, fd;

  memset(&hint, 0, sizeof(hint));
  hint.ai_flags = AI_V4MAPPED | AI_ADDRCONFIG | AI_NUMERICSERV | flags;
  hint.ai_socktype = SOCK_STREAM;

  ret = getaddrinfo(host, port, &hint, &result);
  if (ret != 0) {
    eprintln(error, "getaddrinfo failed: %s", gai_strerror(ret));
    return -2;
  }

  fd = -1;
  for (ai = result; ai != NULL; ai = result->ai_next) {
    fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0)
      continue;

    if (trial(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;

    close(fd);
  }

  freeaddrinfo(result);

  return ai == NULL ? -1 : fd;
}

static int uri_parse(char *uri, size_t n, const char **host, const char **port,
                     const char **loc) {
  char *tmp;

  if (!startswith(uri, "http:/"))
    return -1;

  *host = strtok_r(uri + strlen("http:/"), "/", &tmp);
  if (*host == NULL)
    return -1;

  *loc = MIN(*host + strlen(*host) + 1, uri + n);

  tmp = strchr(*host, ':');
  if (tmp == NULL) {
    *port = "80";
  } else {
    *tmp++ = '\0';
    if (*tmp == '\0')
      return -1;

    if (strchr(tmp, ':') != NULL)
      return -1;

    *port = tmp;
  }

  return 0;
}

static int parse_reqline(char *req, const char **host, const char **port,
                         const char **loc) {
  char *uri, *save;
  const char *tok;

  tok = strtok_r(req, " ", &save);
  if (tok == NULL) {
    eprintln(error, "empty request line");
    return -1;
  }
  if (strcmp(tok, "GET") != 0) {
    eprintln(error, "unknown method '%s'", tok);
    return -1;
  }

  uri = strtok_r(NULL, " ", &save);
  if (uri == NULL) {
    eprintln(error, "missing URI field");
    return -1;
  }

  tok = strtok_r(NULL, " ", &save);
  if (tok == NULL
      || (!startswith(tok, "HTTP/1.1") && !startswith(tok, "HTTP/1.0"))) {
    eprintln(error, "malformed or unknown version field");
    return -1;
  }

  tok = strtok_r(NULL, " ", &save);
  if (tok != NULL && strcmp(tok, "\r\n") != 0) {
    eprintln(error, "malformed request line");
    return -1;
  }

  return uri_parse(uri, strlen(uri), host, port, loc);
}

static int connect_openwb(const char *host, const char *port, FILE **conn) {
  int fd;

  fd = wrap_socket_open(host, port, 0, connect);

  if (fd < 0) {
    eprintln(error, "cannot connect to host");
    *conn = NULL;
    return fd + 1;
  }

  *conn = fdopen(fd, "wb");
  if (*conn == NULL)
    perrorfl("error connecting host");
  return 0;
}

static const char *http_reason(int code) {
  switch (code) {
  case 200:
    return "OK";
  case 301:
    return "Moved Permanently";
  case 302:
    return "Moved Temporarily";
  case 400:
    return "Bad Request";
  case 401:
    return "Unauthorized";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 500:
    return "Internal Server Error";
  case 501:
    return "Not Implemented";
  case 502:
    return "Bad Gateway";
  case 503:
    return "Service Unavailable";
  default:
    return "Unknown";
  }
}

static const char *DAY_NAMES[] = { "Sun", "Mon", "Tue", "Wed",
                                   "Thu", "Fri", "Sat" };
static const char *MONTH_NAMES[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
#define RFC1123_TIME_BUF 30

static int rfc1123_datetimenow(char *buf) {
  struct timeval tv;
  struct tm tm;

  if (gettimeofday(&tv, NULL) != 0 || gmtime_r(&tv.tv_sec, &tm) == NULL)
    return 0;

  strftime(buf, RFC1123_TIME_BUF, "---, %d --- %Y %H:%M:%S GMT", &tm);
  memcpy(buf, DAY_NAMES[tm.tm_wday], 3);
  memcpy(buf + 8, MONTH_NAMES[tm.tm_mon], 3);
  return 1;
}

static void http_response_self(FILE *fp, int code) {
  char buf[RFC1123_TIME_BUF];

  fprintf(fp,
          "HTTP/1.0 %d %s\r\n"
          "Content-Length: 0\r\n",
          code, http_reason(code));

  if (rfc1123_datetimenow(buf))
    fprintf(fp, "Date: %s\r\n", buf);

  fprintf(fp, "\r\n");
}

static int forward_request(FILE *server, FILE *client, char **buf,
                           size_t *buflen, const char *host, const char *loc) {
  ssize_t n;
  int host_sent = 0, ret = 0;

  if ((ret = fprintf(client,
                     "GET /%s HTTP/1.0\r\n"
                     "User-Agent: %s\r\n"
                     "Connection: close\r\n"
                     "Proxy-Connection: close\r\n",
                     loc, user_agent_hdr))
      < 0) {
    goto exit_log;
  }

  eprintln(info, "GET http://%s/%s HTTP/1.0", host, loc);

  while ((n = getline(buf, buflen, server)) > 0) {
    if (startswithcase(*buf, "User-Agent: ")
        || startswithcase(*buf, "Connection: ")
        || startswithcase(*buf, "Proxy-Connection: "))
      continue;
    if (startswithcase(*buf, "Host: "))
      host_sent = 1;
    if (strcmp(*buf, "\r\n") == 0)
      break;

    if ((ret = fwrite_unlocked(*buf, sizeof(**buf), n, client)) == 0) {
      ret = -1;
      goto exit_log;
    }
  }

  if (!host_sent)
    if ((ret = fprintf(client, "Host: %s\r\n", host)) < 0)
      goto exit_log;

  if ((ret = fprintf(client, "\r\n")) < 0)
    goto exit_log;

  ret = fflush_unlocked(client);

exit_log:
  if (ret < 0)
    perrorfl("error sending request");

  return ret;
}

static void forward_response(FILE *client, FILE *server, char *buf,
                             size_t buflen, const char *host, const char *loc) {
  ssize_t n;
  int srcfd, dstfd;

  char *data = malloc(MAX_OBJECT_SIZE);
  size_t datasz = 0;

  srcfd = fileno_unlocked(client);
  dstfd = fileno_unlocked(server);

  while ((n = recv(srcfd, buf, buflen, 0)) != 0) {
    if (n < 0) {
      perrorfl("error receiving message");
      goto exit_free;
    }

    if (datasz <= MAX_OBJECT_SIZE) {
      memcpy(data + datasz, buf, MIN(MAX_OBJECT_SIZE - datasz, n));
      datasz += n;
    }

    if (send(dstfd, buf, n, 0) != n) {
      perrorfl("error sending message");
      goto exit_free;
    }
  }

  if (datasz <= MAX_OBJECT_SIZE)
    cache_put(host, loc, data, datasz);

exit_free:
  free(data);
}

static void handle_connection(FILE *server) {
  char *buf = malloc(MAXLINE);
  size_t buflen = MAXLINE;
  ssize_t n;

  const char *host, *port, *loc;
  FILE *client;

  n = getline(&buf, &buflen, server);
  if (n <= 0) {
    perrorfl("error reading connection");
    http_response_self(server, 400);
    goto no_req;
  }

  if (parse_reqline(buf, &host, &port, &loc) != 0) {
    eprintln(error, "errorneous request line");
    http_response_self(server, 400);
    goto no_req;
  }

  if (cache_get(host, loc, &buf, &buflen)) {
    n = send(fileno_unlocked(server), buf, buflen, 0);
    if (n < 0)
      perrorfl("error sending message");
    goto no_req;
  }

  if (connect_openwb(host, port, &client) != 0) {
    http_response_self(server, 400);
    goto no_req;
  }
  if (client == NULL) {
    http_response_self(server, 502);
    goto no_req;
  }

  host = strdup(host);
  loc = strdup(loc);

  if (forward_request(server, client, &buf, &buflen, host, loc) < 0) {
    http_response_self(server, 502);
    goto req_fail;
  }

  forward_response(client, server, buf, buflen, host, loc);

req_fail:
  free((void *)loc);  // NOLINT(clang-diagnostic-cast-qual)
  free((void *)host); // NOLINT(clang-diagnostic-cast-qual)
  fclose(client);

no_req:
  free(buf);
  fclose(server);
}

static void *worker_loop(void *arg) {
  sbuf_t sbuf = arg;
  FILE *conn;

  while (1) {
    sbuf_deq(sbuf, &conn);
    handle_connection(conn);
  }

  return NULL;
}

static void server_loop(const int listenfd, sbuf_t sbuf) {
  struct sockaddr_storage clientaddr;
  socklen_t clientlen;
  int connfd;
  FILE *conn;

  while (1) {
    connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
    if (connfd < 0) {
      if (errno == EINTR)
        break;

      perrorfl("cannot open connection");
      continue;
    }

    conn = fdopen(connfd, "w+b");
    if (conn == NULL) {
      perrorfl("cannot open connection");
      continue;
    }

    sbuf_enq(sbuf, &conn);
  }
}

static void spawn(int cnt, sbuf_t sbuf) {
  pthread_t worker;
  pthread_attr_t worker_attr;
  int ret, i;

  pthread_attr_init(&worker_attr);
  pthread_attr_setdetachstate(&worker_attr, PTHREAD_CREATE_DETACHED);
  for (i = 0; i < cnt; ++i) {
    ret = pthread_create(&worker, &worker_attr, worker_loop, sbuf);
    if (ret != 0)
      posix_error(ret, "cannot create worke threads");
  }
}

int main(int argc, char *argv[]) {
  char *port;
  int fd;

  struct sigaction sa;
  sbuf_t sbuf;

  port = resolve_port(argc, argv);

  memset(&sa, 0, sizeof(sa));
  sa.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &sa, NULL);

  fd = wrap_socket_open(NULL, port, AI_PASSIVE, sockopt_bind_listen);
  if (fd < 0)
    unix_error("cannot listen to port");

  cache_init(MAX_CACHE_SIZE);
  sbuf = sbuf_create(LISTENQ, sizeof(FILE *));

  spawn(8, sbuf);
  server_loop(fd, sbuf);

  sbuf_destroy(sbuf);
  Close(fd);
  return 0;
}
