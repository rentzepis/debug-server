// @Copyright 2017 Scott Leland Crossen

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"
#include "logger.h"
#include "fdqueue.h"
#include <time.h>

static const char* client_bad_request = "HTTP/1.1 400 Bad Request\r\nServer: Apache\r\nContent-Length: 140\r\nConnection: close\r\nContent-Type: text/html\r\n\r\n<html><head></head><body><p>Bad Request</p></body></html>";
static const char* connection_close = "Connection: close\r\n";
static const char* http_version = "HTTP/1.0\r\n";
static const char* proxy_connection_close = "Proxy-Connection: close\r\n";
static const char* user_agent = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

typedef struct sockaddr_in sockaddr_in;
typedef struct thread_args { cache_t* cache; fdqueue_t* port_queue;} thread_args;

int cache_and_serve(char* buffer, int to_client_fd, int* valid_obj_size, void* cache_content, unsigned int* cache_length, unsigned int length);
int handle_request(int client_fd, cache_t* cache);
int parse_request(char* buffer, char* method, char* protocol, char* host_port, char* resource, char* version);
int process_get_request(cache_t* cache, int fd, char* buffer, char* method, char* resource, rio_t rio_client, char* host_port, int* to_server_fd, char* cache_id, void* cache_content, unsigned int* cache_length);
int process_non_get_request(char* buffer, rio_t rio_client, char* host_port, int* to_server_fd);
int request_from_server(cache_t* cache, int fd, int* to_server_fd, char* cache_id, void* cache_content, unsigned int* cache_length);
int serve_client_and_cache(cache_t* cache, int to_client_fd, int to_server_fd, char* cache_id, void* cache_content);
int serve_client_from_cache(int to_client_fd, void* cache_content, unsigned int cache_length);
int serve_client(int to_client_fd, int to_server_fd);
void close_connection(int* to_client_fd, int* to_server_fd);
void parse_host(char* host_port, char* remote_host, char* remote_port);
void* watch_port_queue_routine(void* arg);

#define READ_FROM_CACHE 1
#define NON_GET_METHOD 2
#define NUM_THREADS 5

int main(int argc, char* argv []) {
  // Initialize server. This is mostly taken directly from tiny server
  char* port;
  int listenfd;
  pthread_t tid;
  sockaddr_in clientaddr;
  socklen_t clientlen;
  thread_args args;

  // Catch the proper signals.
  Signal(SIGPIPE, SIG_IGN);

  // Process input arguments.
  if (argc != 2 ) {
    // User did not supply enough arguments
    fprintf(stderr, "Usage: %s [port]", argv[0]);
    exit(1);
  } else if (atoi(argv[1]) <= 0) {
    // Port is not integer.
    fprintf(stderr, "Port number invalid\n");
    exit(1);
  } else {
    // Proper arguments supplied
    port = argv[1];
    if ((listenfd = Open_listenfd(port)) < 0) {
      // Port in use.
      fprintf(stderr, "Error attaching to port %d\n", *port);
      exit(1);
    } else {
      // Port not in use. Attach and listen.
      args.cache = init_cache();
      init_logger();
      args.port_queue = init_fdqueue();
      for (int i = 0; i < NUM_THREADS; i++) {
        Pthread_create(&tid, NULL, watch_port_queue_routine, &args);
      }
      while (1) {
        // Queue all file descriptors to queue.
        clientlen = sizeof(clientaddr);
        int connfd = Accept(listenfd, (SA*) &clientaddr, (socklen_t*) &clientlen);
        queue_fd(args.port_queue, connfd);
      }
    }
  }
  return 0;
}

void* watch_port_queue_routine(void* vargp) {
  thread_args* args = (thread_args*) vargp;
  int client_fd;

  // Run in deatached mode
  Pthread_detach(pthread_self());
  while(1) {
    // Run until a new file descriptor is available
    if(dequeue_fd(args->port_queue, &client_fd) != -1) {
      handle_request(client_fd, args->cache);
    }
  }
  Pthread_exit(NULL);
  return NULL;
}

int handle_request(int client_fd, cache_t* cache) {
  // Main thread routine when a new connection is encountered.
  char cache_content[MAX_OBJECT_SIZE];
  char cache_id[MAXLINE];
  int ret_val = 0;
  int server_fd = -1;
  unsigned int cache_length;

  // Request the correct content from the server.
  if ((ret_val = request_from_server(cache, client_fd, &server_fd, cache_id, cache_content, &cache_length)) == -1) {
    // Fetch failed
    close_connection(&client_fd, &server_fd);
    log("Error: Server request failed.");
    return -1;
  } else if (ret_val == READ_FROM_CACHE) {
    // Content is cached.
    if (serve_client_from_cache(client_fd, cache_content, cache_length) == -1) {
      close_connection(&client_fd, &server_fd);
      log("Error: Reading from cache failed.");
      return -1;
    }
  } else if (ret_val == NON_GET_METHOD) {
    // Only get methods supported.
    if (serve_client(client_fd, server_fd) == -1) {
      close_connection(&client_fd, &server_fd);
      log("Error: Only get messages supported.");
      return -1;
    }
  } else {
    // Correct branch taken. Cache output when available.
    if (serve_client_and_cache(cache, client_fd, server_fd, cache_id, cache_content) == -1) {
      close_connection(&client_fd, &server_fd);
      log("Error: Failed to serve client.");
      return -1;
    }
  }
  // Success!
  close_connection(&client_fd, &server_fd);
  return 0;
}

int process_get_request(
  cache_t* cache,
  int fd,
  char* buffer,
  char* method,
  char* resource,
  rio_t rio_client,
  char* host_port,
  int* to_server_fd,
  char* cache_id,
  void* cache_content,
  unsigned int* cache_length
) {
  // Initialize local variables
  char origin_host_header[MAXLINE];
  char remote_host[MAXLINE];
  char remote_port[MAXLINE];
  char request_buffer[MAXLINE];

  // Build get request from server.
  strcpy(remote_host, "");
  strcpy(remote_port, "80");
  parse_host(host_port, remote_host, remote_port);
  strcpy(request_buffer, method);
  strcat(request_buffer, " ");
  strcat(request_buffer, resource);
  strcat(request_buffer, " ");
  strcat(request_buffer, http_version);
  // Use helper functions to communicate with server.
  while (Rio_readlineb(&rio_client, buffer, MAXLINE) != 0) {
    // Process the output correctly.
    // Copy over every correct feild.
    if (strcmp(buffer, "\r\n") == 0) {
      break;
    } else if (strstr(buffer, "User-Agent:") != NULL) {
      strcat(request_buffer, user_agent);
    } else if (strstr(buffer, "Connection:") != NULL) {
      strcat(request_buffer, connection_close);
    } else if (strstr(buffer, "Proxy Connection:") != NULL) {
      strcat(request_buffer, proxy_connection_close);
    } else if (strstr(buffer, "Host:") != NULL) {
      strcpy(origin_host_header, buffer);
      if (strlen(remote_host) < 1) {
        sscanf(buffer, "Host: %s", host_port);
        parse_host(host_port, remote_host, remote_port);
      }
      strcat(request_buffer, buffer);
    } else {
      strcat(request_buffer, buffer);
    }
  }
  strcat(request_buffer, "\r\n");
  // Make sure that this host isn't null.
  if (strcmp(remote_host, "") == 0) {
    return -1;
  } else {
    // Build over cached key
    strcpy(cache_id, method);
    strcat(cache_id, " ");
    strcat(cache_id, remote_host);
    strcat(cache_id, ":");
    strcat(cache_id, remote_port);
    strcat(cache_id, resource);
    // Access cached element.
    if (read_data_from_cache(cache, cache_id, cache_content, cache_length) != -1) {
      // Cache contains element. Differ.
      return READ_FROM_CACHE;
    } else {
      // Open port and connect.
      if ((*to_server_fd = Open_clientfd(remote_host, remote_port)) == -1) {
        return -1;
      } else if (*to_server_fd == -2) {
        // Write buffer as appropriate
        strcpy(buffer, client_bad_request);
        Rio_writen(fd, buffer, strlen(buffer));
        return -1;
      } else if (Rio_writen(*to_server_fd, request_buffer, strlen(request_buffer)) == -1) {
        // An error occurred.
        return -1;
      } else {
        return 0;
      }
    }
  }
}

int process_non_get_request(
  char* buffer,
  rio_t rio_client,
  char* host_port,
  int* to_server_fd
) {
  // Initialize local variables
  char origin_host_header[MAXLINE];
  char remote_host[MAXLINE];
  char remote_port[MAXLINE];
  char request_buffer[MAXLINE];
  unsigned int length = 0;
  unsigned int size = 0;

  // Build request correctly.
  strcpy(remote_host, "");
  strcpy(remote_port, "80");
  parse_host(host_port, remote_host, remote_port);
  strcpy(request_buffer, buffer);
  // Process buffer
  while (strcmp(buffer, "\r\n") != 0 && strlen(buffer) > 0) {
    // Use helper functions to process
    if (Rio_readlineb(&rio_client, buffer, MAXLINE) == -1) {
      return -1;
    } else {
      // Build header
      if (strstr(buffer, "Host:") != NULL) {
        strcpy(origin_host_header, buffer);
        if (strlen(remote_host) < 1) {
          sscanf(buffer, "Host: %s", host_port);
          parse_host(host_port, remote_host, remote_port);
        }
      }
      // Continue building header.
      if (strstr(buffer, "Content-Length")) {
        sscanf(buffer, "Content-Length: %d", &size);
      }
      strcat(request_buffer, buffer);
    }
  }
  // Communicate
  if (strcmp(remote_host, "") == 0) {
    // Remove host doesn't exist.
    return -1;
  } else {
    char port = atoi(remote_port);
    // Connect to server and request.
    if ((*to_server_fd = Open_clientfd(remote_host, &port)) < 0) {
      // Connection failed.
      return -1;
    } else if (Rio_writen(*to_server_fd, request_buffer, strlen(request_buffer)) == -1) {
      // Verify wriiten.
      return -1;
    } else {
      // Process paged output too large
      while (size > MAXLINE) {
        if ((length = Rio_readnb(&rio_client, buffer, MAXLINE)) == -1) {
          return -1;
        } else if (Rio_writen(*to_server_fd, buffer, length) == -1) {
          return -1;
        } else {
          size -= MAXLINE;
        }
      }
      // Size in correct range now,
      if (size > 0) {
        if ((length = Rio_readnb(&rio_client, buffer, size)) == -1) {
          return -1;
        } else if (Rio_writen(*to_server_fd, buffer, length) == -1) {
          return -1;
        }
      }
      // Return this to requester.
      return NON_GET_METHOD;
    }
  }
}

int request_from_server(
  cache_t* cache,
  int fd,
  int* to_server_fd,
  char* cache_id,
  void* cache_content,
  unsigned int* cache_length
) {
  // Initialize local variables
  char buffer[MAXLINE];
  char host_port[MAXLINE];
  char method[MAXLINE];
  char resource[MAXLINE];
  rio_t rio_client;

  // Request data from server.
  memset(cache_content, 0, MAX_OBJECT_SIZE);
  Rio_readinitb(&rio_client, fd);
  if (Rio_readlineb(&rio_client, buffer, MAXLINE) == -1) {
    // An error occurred.
    log(buffer);
    return -1;
  } else {
    // Process request.
    char protocol[MAXLINE];
    char version[MAXLINE];
    if (parse_request(buffer, method, protocol, host_port, resource, version) == -1) {
      return -1;
    } else {
      // Make sure the get request works.
      if (strstr(method, "GET") != NULL) {
        // Process the get request.
        return process_get_request(cache, fd, buffer, method, resource, rio_client, host_port, to_server_fd, cache_id, cache_content, cache_length);
      } else {
        // This isn't a get request.
        return process_non_get_request(buffer, rio_client, host_port, to_server_fd);
      }
    }
  }
}

int serve_client(int to_client_fd, int to_server_fd) {
  // Initialize local variables.
  char buffer[MAXLINE];
  rio_t rio_server;
  unsigned int length = 0;
  unsigned int size = 0;

  // Repond to client.
  Rio_readinitb(&rio_server, to_server_fd);
  if (Rio_readlineb(&rio_server, buffer, MAXLINE) == -1) {
    // An error occured on library call.
    return -1;
  } else if (Rio_writen(to_client_fd, buffer, strlen(buffer)) == -1) {
    // An error occured on library call.
    return -1;
  } else {
    // Process body of response.
    while (strcmp(buffer, "\r\n") != 0 && strlen(buffer) > 0) {
      if (Rio_readlineb(&rio_server, buffer, MAXLINE) == -1) {
        return -1;
      } else {
        // Push in content length if available.
        if (strstr(buffer, "Content-Length")) {
          sscanf(buffer, "Content-Length: %d", &size);
        }
        if (Rio_writen(to_client_fd, buffer, strlen(buffer)) == -1) {
          return -1;
        }
      }
    }
    // Response has content.
    if (size > 0) {
      // Process paged data.
      while (size > MAXLINE) {
        if ((length = Rio_readnb(&rio_server, buffer, MAXLINE)) == -1) {
          return -1;
        } else if (Rio_writen(to_client_fd, buffer, length) == -1) {
          return -1;
        } else {
          size -= MAXLINE;
        }
      }
      // Process normally.
      if (size > 0) {
        if ((length = Rio_readnb(&rio_server, buffer, size)) == -1) {
          return -1;
        } else if (Rio_writen(to_client_fd, buffer, length) == -1) {
          return -1;
        }
      }
    } else {
      // Response doesn't ahve content.
      while ((length = Rio_readnb(&rio_server, buffer, MAXLINE)) > 0) {
        // Respond accordingly.
        if (Rio_writen(to_client_fd, buffer, length) == -1) {
          return -1;
        }
      }
    }
    return 0;
  }
}

int serve_client_from_cache(
  int to_client_fd,
  void* cache_content,
  unsigned int cache_length
) {
  // Thank you helper methods.
  if (Rio_writen(to_client_fd, cache_content, cache_length)) {
    return -1;
  } else {
    return 0;
  }
}

int cache_and_serve(
  char* buffer,
  int to_client_fd,
  int* valid_obj_size,
  void* cache_content,
  unsigned int* cache_length,
  unsigned int length
) {
  // Initialize local variables.
  void* current_cache_position;

  if (*valid_obj_size) {
    // Object is valid.
    if ((*cache_length + strlen(buffer)) > MAX_OBJECT_SIZE) {
      *valid_obj_size = 0;
    } else {
      current_cache_position = (void*) ((char*) cache_content + *cache_length);
      memcpy(current_cache_position, buffer, strlen(buffer));
      *cache_length = *cache_length + strlen(buffer);
      *valid_obj_size = 1;
    }
  }
  // Serve from buffer.
  if (Rio_writen(to_client_fd, buffer, length) == -1) {
    return -1;
  } else {
    return 0;
  }
}

int serve_client_and_cache(
  cache_t* cache,
  int to_client_fd,
  int to_server_fd,
  char* cache_id,
  void* cache_content
) {
  // Initialize local variables.
  char buffer[MAXLINE];
  int valid_obj_size = 1;
  rio_t rio_server;
  unsigned int cache_length = 0;
  unsigned int length = 0;
  unsigned int size = 0;

  // Begin main routine.
  Rio_readinitb(&rio_server, to_server_fd);
  if (Rio_readlineb(&rio_server, buffer, MAXLINE) == -1) {
    return -1;
  } else {
    // Can we cache and server normally?
    if(cache_and_serve(buffer, to_client_fd, &valid_obj_size, cache_content, &cache_length, strlen(buffer)) == -1) {
      return -1;
    } else {
      // Process buffer.
      while (strcmp(buffer, "\r\n") != 0 && strlen(buffer) > 0) {
        if (Rio_readlineb(&rio_server, buffer, MAXLINE) == -1) {
          return -1;
        } else {
          // Check content and server if necessary.
          if (strstr(buffer, "Content-Length")) {
            sscanf(buffer, "Content-Length: %d", &size);
          }
          if(cache_and_serve(buffer, to_client_fd, &valid_obj_size, cache_content, &cache_length, strlen(buffer)) == -1) {
            return -1;
          }
        }
      }
      if (size > 0) {
        // Send content if necessary untill size normal. Proceed normally.
        while (size > MAXLINE) {
          if ((length = Rio_readnb(&rio_server, buffer, MAXLINE)) == -1) {
            return -1;
          } else {
            if(cache_and_serve(buffer, to_client_fd, &valid_obj_size, cache_content, &cache_length, length) == -1) {
              return -1;
            } else {
              size -= MAXLINE;
            }
          }
        }
        if (size > 0) {
          // Process with normal service.
          if ((length = Rio_readnb(&rio_server, buffer, size)) == -1) {
            return -1;
          } else if (cache_and_serve(buffer, to_client_fd, &valid_obj_size, cache_content, &cache_length, length) == -1) {
            return -1;
          }
        }
      } else {
        // Content no good.
        while ((length = Rio_readnb(&rio_server, buffer, MAXLINE)) > 0) {
          if(cache_and_serve(buffer, to_client_fd, &valid_obj_size, cache_content, &cache_length, length) == -1) {
            return -1;
          }
        }
      }
      // Syncronously add data to cache.
      if (valid_obj_size && (add_data_to_cache(cache, cache_id, cache_content, cache_length) == -1)) {
        return -1;
      } else {
        return 0;
      }
    }
  }
}

int parse_request (
  char* buffer,
  char* method,
  char* protocol,
  char* host_port,
  char* resource,
  char* version
) {
  // Initialize local variables
  char url[MAXLINE];

  // Not going to lie, I read someone else's code on how to do this part.
  if (strstr(buffer, "/") == NULL || strlen(buffer) < 1) {
    return -1;
  } else {
    // Process request and propulate feilds.
    strcpy(resource, "/");
    sscanf(buffer, "%s %s %s", method, url, version);
    if (strstr(url, "://") != NULL) {
      sscanf(url, "%[^:]://%[^/]%s", protocol, host_port, resource);
    } else {
      sscanf(url, "%[^/]%s", host_port, resource);
    }
    return 0;
  }
}

void parse_host(char* host_port, char* remote_host, char* remote_port) {
  // Initialize local variables.
  char* tmp = NULL;

  // Grab host port.
  tmp = index(host_port, ':');
  if (tmp != NULL) {
    *tmp = '\0';
    strcpy(remote_port, tmp + 1);
  } else {
    strcpy(remote_port, "80");
  }
  // Copy appropriately
  strcpy(remote_host, host_port);
}

void close_connection(int* to_client_fd, int* to_server_fd) {
  // Thread exited. Close connections.
  if (*to_client_fd >= 0) {
    // Close client connection.
    Close(*to_client_fd);
    to_client_fd = NULL;
  }
  if (*to_server_fd >= 0) {
    // Close server connection.
    Close(*to_server_fd);
    to_server_fd = NULL;
  }
}
