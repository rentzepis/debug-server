/*
 * 15-213:
 *   Proxy lab
 *
 * Partners:
 *   Michael Choquette - mchoquet
 *   Karan Sikka       - ksikka
 *
 */

#include "proxy.h"

static const char *user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding = "Accept-Encoding: gzip, deflate\r\n";

struct prox_params {
  struct sockaddr_in clientaddr;
  int clientfd;
};
// for counting threads, debugging only
static int thread_cnt = 0;
static pthread_mutex_t thread_cnt_mutex;

// Takes the listening port as the first argument
//    greater than 1000, less than 64000
int main(int argc, char** argv) {
  int listenfd, port;

  printf("Welcome to proxy.\n");
  #ifdef DEBUG
  pthread_mutex_init(&thread_cnt_mutex,NULL);
  printf("Running in debug mode. Remove '#define DEBUG' to exit.\n");
  #else
  printf("To run in debug mode, add '#define DEBUG'.\n");
  #endif

  /* Check command line args */
  if (argc != 2) {
	  fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }
  port = atoi(argv[1]);

  /* opens the port for clients to connect via */
  listenfd = Open_listenfd(port);

  /* initialize mutex for csapp.c, block SIGPIPE */
  init_gethostbyname_mutex();
  block_sigpipe();

  /* initialize cache */
  if (initCache(MAX_OBJECT_SIZE, MAX_CACHE_SIZE) != 0) {
    printf("could not initialize cache. Aboritng...\n");
    exit(1);
  }

  /* connection handling loop */
  while (1) {
    struct sockaddr_in clientaddr;
    struct prox_params * attr = Calloc(1,sizeof(struct prox_params));

    /* wait for a connection */
	  socklen_t clientlen = sizeof(clientaddr);
	  attr -> clientfd = Accept(listenfd, (SA *)&(attr -> clientaddr), &clientlen);
    if (attr -> clientfd < 0) {
      free(attr);
      continue;
    }
    dbg_printf("Accepted client connection...\n");

    pthread_t tid;
    /* spawn a new thread, which will detach itself */
    if (pthread_create(&tid, NULL, (void *) &handle_proxy_request, attr) != 0) {
      app_error("Thread error. Skipping this request.\n");
      Close(attr -> clientfd);
      free(attr);
      continue;
    }

    #ifdef DEBUG
    pthread_mutex_lock(&thread_cnt_mutex);
    thread_cnt++;
    dbg_printf("Thread count: %d\n",thread_cnt);
    pthread_mutex_unlock(&thread_cnt_mutex);
    #endif
  }
  return 0;
}

/* Decrements the global counter for # of threads.
 * Only called if "#define DEBUG" is present. */
void decrement_thread_cnt() {
  pthread_mutex_lock(&thread_cnt_mutex);
  thread_cnt--;
  dbg_printf("RIP thread. Count: %d\n",thread_cnt);
  pthread_mutex_unlock(&thread_cnt_mutex);
}

/*
 * Reads the first line into the given buffer.
 * If the first line is too long it will only read part of it.
 */
int readHeaderLine(rio_t* clientRio, char* headerLine) {
  if (Rio_readlineb(clientRio, headerLine, MAXLINE) < 0) return -1;
  headerLine[MAXLINE-1] = '\0';
  return 0;
}

/*
 * Given a linked list of lines, parses HTTP request and stores
 * the extracted information in the struct.
 *
 * Returns -1 on error (no get request, not http, etc)
 */
// TODO

/*
 * Given a clientRio, return a linked list of lines.
 */
struct line_list* readRequest(rio_t* clientRio)
{
  struct line_list* start_line = calloc(1, sizeof(struct line_list));
  if (start_line == NULL) return NULL;
  struct line_list* curr_line = start_line;

  int line_length;
  char buf[MAXLINE];

  while((line_length = Rio_readlineb(clientRio, buf, MAXLINE)) > 0) {
    if (startsWith(buf,"\r\n")) break;
    curr_line -> next = Calloc(1,sizeof(struct line_list));
    curr_line = curr_line -> next;
    if (curr_line == NULL) return NULL;
    strncpy(curr_line -> line, buf, MAXLINE);
    memset(buf, '\0', MAXLINE);
    dbg_printf("Read a line from the client: %d\n",line_length);
  }
  // free the first line and move forward one 
  curr_line = start_line->next;
  free(start_line);

  if (line_length == -1) return NULL;

  return curr_line;
}

/*
 * Free a line_list
 */
void free_line_list(struct line_list* list) {
  struct line_list* temp = list;
  while(list != NULL) {
    temp = list -> next;
    free(list);
    list = temp;
  }
}

/* Function to take a proxy request and fulfil it's needs.
 * Run by threads spawned in main. Threadsafe. */
void handle_proxy_request (void* attr) {
  pthread_detach(pthread_self()); // detach because main doesn't join
  struct prox_params * params = (struct prox_params *) attr;
  if (params == NULL) {
    dbg_decrement_thread_cnt(); return;
  }
  int clientfd = params -> clientfd;

  rio_t clientRio;
  Rio_readinitb(&clientRio, clientfd);

  struct line_list* request_lines;
  struct http_req request_parsed;

  /* read the request from client */
  if ((request_lines = readRequest(&clientRio)) == NULL) {
    dbg_decrement_thread_cnt();
    return;
  }
  /* parse request lines */
  if (parseRequest(request_lines, &request_parsed) < 0) {
    dbg_decrement_thread_cnt();
    free_line_list(request_lines);
    return;
  }
  
  /* search the cache */
  void* response; int responseLen;
  struct cache_key key;
    key.hostName = request_parsed.hostname;
    key.serverPort = request_parsed.serverPort;
    key.filePath = request_parsed.filePath;
  if ((response = lookup(&key, &responseLen)) != NULL) {
    dbg_printf("Cache Hit!\n");
    forwardCachedResponse(clientfd, response, responseLen);
    Close(clientfd);
    free(params);
    free_line_list(request_lines);
    dbg_decrement_thread_cnt();
    return;
  }

  /* otherwise, get the content from the server as normal */
  int error = 0;
  int serverfd = -1;

  /* send user's request to the server */
  serverfd = forwardRequest(&clientRio, &request_parsed, &error);
  if (error) {
    app_error("error while forwarding request\n");
    Close(clientfd);
    if (serverfd >= 0) Close(serverfd);
    free(params);
    free_line_list(request_lines);
    dbg_decrement_thread_cnt();
    return;
  }
  /* send server's reply to the user */
  if (forwardResponse(clientfd, &request_parsed, serverfd) == -1) {
    app_error("error while forwarding response\n");
    Close(clientfd);
    Close(serverfd);
    free(params);
    free_line_list(request_lines);
    dbg_decrement_thread_cnt();
    return;
  }
  /* close connections */
  Close(serverfd);
  Close(clientfd);
  dbg_printf("Closed connection. Goodbye\n");
  free(params);
  free_line_list(request_lines);
  dbg_decrement_thread_cnt();
  return;
}


/*
 * Reads a request from the client, and writes it to the server.
 * Returns the file descriptor of the connection with the server
 */
int forwardRequest(rio_t* clientRio, struct http_req* request_parsed, int* errPtr) {
  /* gets ready to read */
  int serverPort = request_parsed -> serverPort;
  char* hostname = request_parsed -> hostname;
  char* filePath = request_parsed -> filePath;

  *errPtr = 1; /* guilty until proven innocent */

  /* open a connection to the server */
  int serverfd = Open_clientfd(hostname, serverPort);
  if (serverfd < 0) return -1;
  dbg_printf("opened connection to server\n");

  /* write out the request line, headers, and trailing blank line */
  if (sendRequestLine(serverfd, filePath) < 0) return -1;
  dbg_printf("sent req line\n");

  if (sendRequiredHeaders(serverfd) < 0) return -1;
  dbg_printf("sent required headrs\n");

  int sentHostHeader = sendRemainingHeaders(request_parsed -> otherHeaders, serverfd);
  if (sentHostHeader < 0) return -1;
  dbg_printf("sent rest of headrs\n");

  if (sentHostHeader == 0) {
    if (sendHostHeader(serverfd, hostname) < 0) return -1;
  }
  dbg_printf("sent host header\n");

  if (Rio_writen(serverfd, "\r\n", strlen("\r\n")) < 0) return -1; 
  dbg_printf("sent newline\n");

  *errPtr = 0;
  return serverfd;
}

/*
 * Reads the response from the server and writes it to the client
 */
int forwardResponse(int clientfd, struct http_req* request_parsed, int serverfd) {
  /* read the server response, and write it to the client (as-is) */
  /* the choice of MAXLINE was arbitrary; I'm not really reading by line */
  char serverBuf[CHUNK_SIZE];
  char cacheEntry[MAX_OBJECT_SIZE];
  rio_t serverRio;
  rio_readinitb(&serverRio, serverfd);
  dbg_printf("init\n");
  /* stream data to client */
  int amtRead, amtWrote, responseSize = 0;
  while ((amtRead = rio_readnb(&serverRio, serverBuf, CHUNK_SIZE)) > 0) {
    dbg_printf("read %d\n", amtRead);
    if ((amtWrote = rio_writen(clientfd, serverBuf, amtRead)) == -1) return -1;
    dbg_printf("wrote %d\n", amtWrote);
    if (responseSize + amtWrote <= MAX_OBJECT_SIZE) {
      memcpy(cacheEntry + responseSize, serverBuf, amtWrote);
    }
    responseSize += amtWrote;
  }

  /* add response to cache */
  if (responseSize <= MAX_OBJECT_SIZE) {
    struct cache_key key;
      key.hostName = request_parsed -> hostname;
      key.serverPort = request_parsed -> serverPort;
      key.filePath = request_parsed -> filePath;
    if (update(&key, cacheEntry, responseSize) != 0) {
      dbg_printf("Error writing information to cache\n");
      return -1;
    }
    dbg_printf("wrote %d bytes to the cache\n", responseSize);
  } else {
    dbg_printf("response of %d bytes did not fit in the cache\n", responseSize);
  }

  return 0;
}

/*
 * Forwards the response gotten from the cache to the client
 */
int forwardCachedResponse(int clientfd, void* response, int responseLen) {
  int i = 0;
  /* write every chunk but the last one */
  while (i + CHUNK_SIZE <= responseLen) {
    if (rio_writen(clientfd, response + i, CHUNK_SIZE) < 0) {
      app_error("write error while forwarding cached response to client.\n");
      return -1;
    }
    i += CHUNK_SIZE;
  }
  /* write the last chunk */
  if (rio_writen(clientfd, response + i, responseLen - i) < 0) {
    app_error("write error while forwarding cached response to client.\n");
    return -1;
  }
  return 0;
}


int sendRequestLine(int serverfd, char* filePath) {
  char s[MAXLINE]; 
  strncpy(s, "GET ", MAXLINE);
  strcat(s, filePath);
  strcat(s, " HTTP/1.0\r\n");
  if (rio_writen(serverfd, s, strlen(s)) < strlen(s))
  {
    unix_error("Error sending request line to server.");
    return -1;
  }
  return 0;
}

/*
 * Concatenates the required headers together and sends them.
 */
int sendRequiredHeaders(int serverfd) {
  char s[MAXLINE * 5];
  strncpy(s, user_agent, MAXLINE * 5);
  strcat(s, accept_hdr);
  strcat(s, accept_encoding);
  strcat(s, "Connection: close\r\n");
  strcat(s, "Proxy-Connection: close\r\n");
  if (Rio_writen(serverfd, s, strlen(s)) < 0)
    return -1;

  return 0;
}

/*
 * Splits the request (in buf) at ':', and puts into name and val
 * Returns 0 on success, or else -1
 */
int parseRequestHeader(char buf[], char nameBuf[], char valBuf[]) {
  int i=0;
  while (buf[i] != '\0') {
    if (buf[i] == ':') {
      strncpy(nameBuf, buf, i);
      strcpy(valBuf, buf + i + 2); /* +2 to bypass the ": " */
      return 0;
    }
    i++;
  }
  return -1;
}

/*
 * Returns whether or not the header is one of the hardcoded headers.
 * (If it is, it was already sent, so shouldn't be forwarded again)
 */
int shouldIgnoreHeader(char h[]) {
  char* headers[5] = {"User-Agent","Accept", "Accept-Encoding",
                      "Connection","Proxy-Connection"};
  int i=0;
  while (i < 5) {
    if (!strcmp(h, headers[i])) return 1;
    i++;
  }
  return 0;
}

/*
 * Reads all the client headers and forwards the unprocessed ones.
 * Returns whether or not it forwarded a HOST header
 */
int sendRemainingHeaders(struct line_list* otherHeaders, int serverfd) {
  char nameBuf[MAXLINE];
  char valBuf[MAXLINE];
  int sentHost = 0;
  struct line_list* curr_line = otherHeaders;
  while (curr_line != NULL) {
    char* buf = curr_line -> line;
    if (parseRequestHeader(buf, nameBuf, valBuf) != 0) {
      dbg_printf("bad request header: %s\n", buf);
      return -1;
    }
    if (!shouldIgnoreHeader(nameBuf)) {
      if (Rio_writen(serverfd, buf, strlen(buf)) == -1)
        return -1;
      if (!strcmp(nameBuf, "Host")) sentHost++;
    }
    curr_line = curr_line -> next;
  }

  return sentHost;
}

/*
 * Writes the default host to the server, if none was provided
 */
int sendHostHeader(int serverfd, char* hostname) {
  char s[MAXLINE];
  strncpy(s, "Host: ", MAXLINE);
  strcat(s, hostname);
  strcat(s, "\r\n");
  if( Rio_writen(serverfd, s, strlen(s)) < 0) return -1;

  dbg_printf("\nSENT DEFAULT HOST\nHost: %s\r\n", hostname);
  return 0;
}

/* Blocks SIGPIPE so server doesn't die.
 * Error is propagated manually. */
void block_sigpipe() {
  sigset_t sigset;
  sigemptyset(&sigset);
  sigaddset(&sigset, SIGPIPE);
  sigprocmask(SIG_BLOCK, &sigset, NULL);
}
