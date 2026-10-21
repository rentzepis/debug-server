#include <ctype.h>
#include <stdio.h>

#include "csapp.h"

typedef enum {
  GET,
  HEAD,
  POST,
  PUT,
  DELETE,
  CONNECT,
  OPTIONS,
  TRACE,
  PATCH,
  UNKNOWN
} method_t;

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MIN(x, y) ((x) < (y) ? (x) : (y))

/* You won't lose style points for including this long line in your code */
static const char *g_user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3";
static const char *g_conn_hdr = "Connection: close";
static const char *g_proxy_conn_hdr = "Proxy-Connection: close";
static const char *g_version_hdr = "HTTP/1.0";
static char *g_listen_port = NULL;

/**SECTION - Function Declarations*/
/***/

/// @brief do recv client request, send server request, recv server response,
/// send server response yadda yadda
/// @param fd connected descriptor, which can communicate with the client
void doit(int fd);

/// @brief Read request headers except the first line
/// @param rp RIO object that can read robustly
/// @param host value of the key `HOST`
void read_requesthdrs(rio_t *rp, char *host, size_t hostlen);

/// @brief Proxy server assums that uri MUST include whole valid URI of
/// endpoint.
/// @param uri [in] whole URI such as
/// `http://cmu.edu.net/foo/bar?some&additional&arguments`
/// @param host [out] caller-initialize buffer that saves host section from full
/// URI
/// @param hostlen length of `host`
/// @param path [out] caller-initialize buffer that saves except host section
/// from full URI
/// @param pathlen length of `path`
/// @return 1 if uri is valid, 0 if uri is invalid

/// @brief Proxy server assums that uri MUST include whole valid URI of
/// endpoint.
void parse_uri(const char *uri, char *proto, size_t protolen, char *host,
               size_t hostlen, char *port, size_t portlen, char *path,
               size_t pathlen);

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg);

/// @brief split into two parts specified with delimeter
/// @param line [in]
/// @param left [out]
/// @param right [out]
/// @return 1 if success, 0 if failure, no delimeter found
int split(const char *line, char *left, size_t leftlen, char *right,
          size_t rightlen, const char delim);

/// @brief split into two parts specified wit delimeter
/// @param line [in]
/// @param left [out]
/// @param right [out]
/// @param delim different with `split`, it takes string, not character
/// @return 1 if success, 0 if failure, no delimeter found
int splitstr(const char *line, char *left, size_t leftlen, char *right,
             size_t rightlen, const char *delim, size_t delimlen);

/// @brief get method from request header line
/// @return method_t, an enum type
inline static method_t __get_method(char *header, size_t len);

/// @brief This function returns a pointer to a substring of the original
/// string. If the given string was allocated dynamically, the caller must not
/// overwrite that pointer with the returned value, since the original pointer
/// must be deallocated using the same allocator with which it was allocated.
/// The return value must NOT be deallocated using free() etc.
/// @note https://stackoverflow.com/a/122721/21369350
char *trimwhitespace(char *str);
/***/
/**!SECTION - Function Declarations*/

#ifndef LIB
int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  // automatically call socket(2), bind(2), listen(2)
  g_listen_port = argv[1];
  listenfd = Open_listenfd(g_listen_port);

  while (1) {
    clientlen = sizeof(clientaddr);
    // TODO - Part II. Dealing with multiple concurrent requests
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("[*] Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }

  return 0;
}
#endif  // LIB

void doit(int client_fd) {
  int server_fd;  // talk with server
  rio_t rio_c2p;  // client to proxy
  rio_t rio_s2p;  // server to proxy
  char buf[MAXLINE], method_str[MAXLINE], uri_str[MAXLINE],
      version_str[MAXLINE], req_port[MAXLINE];
  method_t method;

  // read request headers
  Rio_readinitb(&rio_c2p, client_fd);
  Rio_readlineb(&rio_c2p, buf, MAXLINE);
  printf("[*] Request headers: \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method_str, uri_str, version_str);

  if ((method = __get_method(method_str, strlen(method_str) + 1)) ==
      (method_t)UNKNOWN) {
    clienterror(client_fd, method_str, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }

  // parse uri into host & path from client request
  char proto_str[MAXLINE], host_str[MAXLINE], port_str[MAXLINE],
      path_str[MAXLINE];
  parse_uri((const char *)uri_str, proto_str, MAXLINE, host_str, MAXLINE,
            port_str, MAXLINE, path_str, MAXLINE);

  if (port_str[0] == '\0') {
    // empty port means default port
    strcpy(port_str, "80");
  }

  if (strcmp(proto_str, "http") != 0) {
    clienterror(client_fd, proto_str, "501", "Not Implemented",
                "Tiny supports only for http protocol");
    return;
  }

  // NOTE - host may be overrided by HOST attribute!!
  read_requesthdrs(&rio_c2p, host_str, MAXLINE);

  server_fd = Open_clientfd(host_str, port_str);

  // send request to host server
  char req_buf[MAXBUF] = {0};
  sprintf(req_buf, "%s %s %s\r\n", method_str, path_str, g_version_hdr);
  sprintf(req_buf, "%sHost: %s\r\n", req_buf, host_str);
  sprintf(req_buf, "%s%s\r\n", req_buf, g_user_agent_hdr);
  sprintf(req_buf, "%s%s\r\n", req_buf, g_conn_hdr);
  sprintf(req_buf, "%s%s\r\n", req_buf, g_proxy_conn_hdr);
  sprintf(req_buf, "%s\r\n", req_buf);

  printf("[*] forwarded headers:\n%s\n", req_buf);

  Rio_writen(server_fd, req_buf, MAXLINE);

  // receive response
  memset(buf, 0, sizeof(buf));
  Rio_readinitb(&rio_s2p, server_fd);
  printf("[*] response headers:\n");

  ssize_t n;
  // while ((n = Rio_readlineb(&rio_s2p, buf, MAXLINE)) > 0 &&
  //        strncmp(buf, "\r\n", 2) != 0) {
  //   // forward headers
  //   printf("%s", buf);
  //   Rio_writen(client_fd, buf, n);
  // }

  while ((n = Rio_readlineb(&rio_s2p, buf, MAXLINE)) > 0) {
    // forward to client
    printf("%s", buf);
    Rio_writen(client_fd, buf, n);
  }
  Close(server_fd);
}

/// @brief split into two parts specified with delimeter
/// @param line [in]
/// @param left [out]
/// @param right [out]
/// @return 1 if success, 0 if failure, no delimeter found
int split(const char *line, char *left, size_t leftlen, char *right,
          size_t rightlen, const char delim) {
  const char *pos = strchr(line, (int)delim);
  if (pos == NULL) {
    pos = line + strlen(line);
  }

  // do copy left

  size_t idx = 0;
  for (const char *itr = line; (itr - line) < leftlen && *itr != delim; ++itr) {
    if (isspace(*itr)) continue;
    left[idx++] = *itr;
  }

  // do copy right
  strncpy(right, pos + 1, rightlen);
  return 1;
}

int splitstr(const char *line, char *left, size_t leftlen, char *right,
             size_t rightlen, const char *delim, size_t delimlen) {
  const char *pos = strstr(line, delim);
  if (pos == NULL) {
    pos = line + strlen(line);
  }

  // do copy left
  for (const char *itr = line; itr != pos; ++itr) {
    size_t idx = itr - line;
    if (isspace(*itr)) continue;
    left[idx] = *itr;
  }

  // do copy right
  strncpy(right, pos + delimlen, rightlen);
  return 1;
}

void read_requesthdrs(rio_t *rp, char *host, size_t hostlen) {
  char buf[MAXLINE], key[MAXLINE], value[MAXLINE];

  while (Rio_readlineb(rp, buf, MAXLINE) > 0 && strcmp(buf, "\r\n") != 0) {
    printf("%s", buf);

    split((const char *)buf, key, MAXLINE, value, MAXLINE, ':');

    if (strncasecmp(key, "HOST", 4) == 0) {
      // if header HOST found, copy value into `host`

      split(value, host, hostlen, buf, MAXLINE, ':');
    }
  }
}

void parse_uri(const char *uri, char *proto, size_t protolen, char *host,
               size_t hostlen, char *port, size_t portlen, char *path,
               size_t pathlen) {
  char tmpbuf[MAXLINE] = {0}, tmpbuf2[MAXLINE] = {0};
  path[0] = '/';
  // proto & others
  splitstr(uri, proto, protolen, tmpbuf, MAXLINE, "://", 3);
  // others & path[option]
  split(tmpbuf, tmpbuf2, MAXLINE, path + 1, pathlen - 1, '/');
  // host and port[option]
  split(tmpbuf2, host, hostlen, port, portlen, ':');
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg,
                 char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // Build the HTTP response body
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body,
          "%s<body bgcolor="
          "ff1111"
          ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // Print the HTTP response
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-Length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

inline method_t __get_method(char *header, size_t len) {
  // read first bytes and check
  if (strncmp(header, "GET", 3) == 0) {
    return GET;
  } else if (strncmp(header, "HEAD", 4) == 0) {
    return HEAD;
  } else if (strncmp(header, "POST", 4) == 0) {
    return POST;
  } else if (strncmp(header, "PUT", 3) == 0) {
    return PUT;
  } else if (strncmp(header, "DELETE", 6) == 0) {
    return DELETE;
  } else if (strncmp(header, "CONNECT", 7) == 0) {
    return CONNECT;
  } else if (strncmp(header, "OPTIONS", 7) == 0) {
    return OPTIONS;
  } else if (strncmp(header, "TRACE", 5) == 0) {
    return TRACE;
  } else if (strncmp(header, "PATCH", 5) == 0) {
    return PATCH;
  }
  return UNKNOWN;
}

char *trimwhitespace(char *str) {
  char *end;

  // Trim leading space
  while (isspace((unsigned char)*str)) str++;

  if (*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}