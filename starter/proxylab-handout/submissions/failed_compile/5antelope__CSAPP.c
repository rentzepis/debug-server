#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

jmp_buf pipe_buf; /* non-local return for SIGPIPE handler */
jmp_buf error_buf; /* non-local return for IO error */

/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

/* Function prototypes */

/* SIGPIPE handler */
void sigpipe_handler(int sig);
void routine(void *connfd);
int Send(int fd, int *server_fd, char *cache_tag, void *cache_data, unsigned int *cache_length);
int fetch_cache(int client_fd, void *cache_data, unsigned int cache_length);
int in_GET(int client_fd, int server_fd, char *cache_tag, void *cache_data);
void parse_request(char *buf, char *method, char *protocol, char *host_port, char *resource, char *version);
void parse_port(char *host_port, char *remote_host, char *remote_port);
int Append(char *content, unsigned int *content_length, char *buf);
void get_size(char *buf, unsigned int *size_pointer);
/* End of function prototypes */

cache_t *cache = NULL;

/* Main function */
int main(int argc, char *argv[]) {
    int listenfd, port, clientlen, *connfd; // use pointer for connfd: cuz of malloc
    struct sockaddr_in clientaddr;
    pthread_t tid;

    /* installing signal handler */
    Signal(SIGPIPE, sigpipe_handler);

    /* Check command line args */
	if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(1);
	}

	port = atoi(argv[1]); // port number from command line
	cache = (cache_t *)malloc(sizeof(cache_t));
	init_cache(cache); // initiate proxy cache

	if ((listenfd = Open_listenfd(port)) < 0) {
		fprintf(stderr, "invalidate port: %d\n", port);
	}
	
	while (1) {
	    clientlen = sizeof(clientaddr);
	    int c;
	    for (c=0; (connfd = (int *)malloc(sizeof(int))) == NULL; c++) {
	    	if (c>10)
	    		break; // MAX_CACHE_SIZE = 1 MiB	    	
	    }
	    *connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
	    // create a new thread.. routine: 
	    Pthread_create(&tid, NULL, (void * )routine, (void *)connfd);
	    
	}
    return 0;
}

void sigpipe_handler(int sig) {
	//unfinished...
    siglongjmp(pipe_buf, -1);
}

/*
 * thread's routine when it is invoked
 */
void routine(void *connfd) {
	Pthread_detach(pthread_self()); // 

	unsigned int cache_length;
	char cache_tag[MAXLINE];
	char cache_data[MAX_OBJECT_SIZE];
	int client_fd = *(int *)connfd;
	int server_fd = -1;
    int rp;
    
    rp = sigsetjmp(pipe_buf, 1);
    
	int ack = Send(client_fd, &server_fd, cache_tag, cache_data, &cache_length);

	switch (ack) {
		case 0: // cache hit
			fetch_cache(client_fd, cache_data, cache_length);
			break;
		default: // GET
			in_GET(client_fd, server_fd, cache_tag, cache_data);
	}

	if (client_fd >= 0) {
        Close(client_fd);
    }
    if (server_fd >= 0) {
        Close(server_fd);
    }

	return;
}

int Send(int fd, int *server_fd, char *cache_tag, void *cache_data, unsigned int *cache_length) {
	char buf[MAXLINE], 
         init_request[MAXLINE],
         init_header[MAXLINE],
         request_buf[MAXLINE],         
         method[MAXLINE], 
         protocol[MAXLINE],
         host_port[MAXLINE],
         remote_host[MAXLINE], 
         remote_port[MAXLINE], 
         resource[MAXLINE],
         version[MAXLINE];

    rio_t rio_client;

    strcpy(remote_host, "");
    strcpy(remote_port, "80");

    memset(cache_data, 0, MAX_OBJECT_SIZE);

    Rio_readinitb(&rio_client, fd);

    Rio_readlineb(&rio_client, buf, MAXLINE);

    strcpy(init_request, buf);

    parse_request(buf, method, protocol, host_port, resource, version);

    parse_port(host_port, remote_host, remote_port);

    if (strstr(method, "GET") != NULL) { // GET method
        // request line
        strcpy(request_buf, method);
        strcat(request_buf, " ");
        strcat(request_buf, resource);
        strcat(request_buf, " ");

        // request header
        while (Rio_readlineb(&rio_client, buf, MAXLINE) != 0) {
            if (strcmp(buf, "\r\n") == 0) {
                break;
            } else if (strstr(buf, "User-Agent:") != NULL) {
                strcat(request_buf, user_agent_hdr);                
            } else if (strstr(buf, "Accept-Encoding:") != NULL) {
                strcat(request_buf, accept_encoding_hdr);
            } else if (strstr(buf, "Accept:") != NULL) {
                strcat(request_buf, accept_hdr);
            } else if (strstr(buf, "Connection:") != NULL) {
                strcat(request_buf, connection_hdr);
            } else if (strstr(buf, "Proxy Connection:") != NULL) {
                strcat(request_buf, proxy_connection_hdr);
            } else if (strstr(buf, "Host:") != NULL) {
                strcpy(init_header, buf);
                if (strlen(remote_host) < 1) {
                    // if host not specified in request line, get host from host header
                    sscanf(buf, "Host: %s", host_port);
                    parse_port(host_port, remote_host, remote_port);
                }
                strcat(request_buf, buf);
            } else {
                strcat(request_buf, buf);
            }
        }
        
        strcat(request_buf, "\r\n");

        // compose cache id
        strcpy(cache_tag, method);
        strcat(cache_tag, " ");
        strcat(cache_tag, remote_host);
        strcat(cache_tag, ":");
        strcat(cache_tag, remote_port);
        strcat(cache_tag, resource);
        strcat(cache_tag, " ");
        strcat(cache_tag, version);

        // search in the cache
        cache_node *node = find(cache, cache_tag);
        if (node != NULL) { // cache hit
            *cache_length = node->size;
            memcpy(cache_data, node->data, *cache_length);

            // LRU policy
            P(&(cache->c_mutex));
            node = evict(cache, cache_tag);
            insert(cache, node);
            V(&(cache->c_mutex));

            return 0;
        } else {
            P(&(cache->w_mutex));
                cache->readcnt--;
                if (cache->readcnt == 0) {
                    V(&(cache->c_mutex));
                }
                V(&(cache->w_mutex));
        }
        // client to server
        *server_fd = Open_clientfd(remote_host, atoi(remote_port));
        Rio_writen(*server_fd, request_buf, strlen(request_buf));

        return 1;
    } else {
        // non GET method
        unsigned int length = 0, size = 0;
        strcpy(request_buf, buf);
        while (strcmp(buf, "\r\n") != 0 && strlen(buf) > 0) {
            if (Rio_readlineb(&rio_client, buf, MAXLINE) == -1) {
                return -1;
            }
            if (strstr(buf, "Host:") != NULL) {
                strcpy(init_header, buf);
            }
            get_size(buf, &size);
            strcat(request_buf, buf);
        }
        
        *server_fd = Open_clientfd(remote_host, atoi(remote_port));

        // write request line
        Rio_writen(*server_fd, request_buf, strlen(request_buf));

        // write request body
        while (size > MAXLINE) {
            length = Rio_readnb(&rio_client, buf, MAXLINE);
            Rio_writen(*server_fd, buf, length);

            size -= MAXLINE;
        }
        if (size > 0) {
            length = Rio_readnb(&rio_client, buf, size);
            Rio_writen(*server_fd, buf, length);

        }
        return 1;
    }
}

int fetch_cache(int client_fd, void *cache_data, unsigned int cache_length) {
    // forward from cache
    Rio_writen(client_fd, cache_data, cache_length);

    return 0;
}

int in_GET(int client_fd, int server_fd, char *cache_tag, void *cache_data) {
	rio_t rio_server;

    char buf[MAXBUF];
    unsigned int cache_length = 0, length = 0, size = 0;

    int flag = 1;

    Rio_readinitb(&rio_server, server_fd);
    // forward status line and write to cache_data
    if (Rio_readlineb(&rio_server, buf, MAXLINE) == -1) {
        return -1;
    }
    if (flag) {
        flag = Append(cache_data, &cache_length, buf);
    }
    Rio_writen(client_fd, buf, strlen(buf));

    // forward response headers and write to cache_data
    while (strcmp(buf, "\r\n") != 0 && strlen(buf) > 0) {
        if (Rio_readlineb(&rio_server, buf, MAXLINE) == -1) {
            return -1;
        }
        get_size(buf, &size);
        if (flag) {
            flag = Append(cache_data, &cache_length, buf);
        }
        Rio_writen(client_fd, buf, strlen(buf));
    }
    // forward response body and write to cache_data
    if (size > 0) {
        while (size > MAXLINE) {
            if ((length = Rio_readnb(&rio_server, buf, MAXLINE)) == -1) {
                return -1;
            }
            if (flag) {
                flag = Append(cache_data, &cache_length, buf);
            }
            Rio_writen(client_fd, buf, length);

            size -= MAXLINE;
        }
        if (size > 0) {
            if ((length = Rio_readnb(&rio_server, buf, size)) == -1) {
                return -1;
            }
            if (flag) {
                flag = Append(cache_data, &cache_length, buf);
            }
            Rio_writen(client_fd, buf, length);

        }
    } else {
        while ((length = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
            if (flag) {
                flag = Append(cache_data, &cache_length, buf);
            }
            Rio_writen(client_fd, buf, length);

        }
    }
    // write cache_data to cache when size smaller than MAX_OBJECT_SIZE
    if (flag) {
        
        cache_node *node = (cache_node *)malloc(sizeof(cache_node));
        node->tag = (char *)Malloc(sizeof(char) * (strlen(cache_tag) + 1));
        
        strcpy(node->tag, cache_tag);
        node->size = 0;
        node->data = Malloc(cache_length);
        node->next = NULL;


        memcpy(node->data, cache_data, cache_length);
        node->size = cache_length;
        insert(cache, node);
        
        return 0;
    }
    
    return 0;
}

/*
 * parse_request
 */
void parse_request(char *buf, char *method, char *protocol, char *host_port, char *resource, char *version) {
    char url[MAXLINE];
    // set resource default to '/'
    strcpy(resource, "/");
    sscanf(buf, "%s %s %s", method, url, version);
    if (strstr(url, "://") != NULL) {
        // has protocol
        sscanf(url, "%[^:]://%[^/]%s", protocol, host_port, resource);
    } else {
        // no protocols
        sscanf(url, "%[^/]%s", host_port, resource);
    }
}

/*
 * parse_port - parse host:port (:port optional)to two parts
 */
void parse_port(char *host_port, char *remote_host, char *remote_port) {
    char *tmp = NULL;
    tmp = index(host_port, ':');
    if (tmp != NULL) {
        *tmp = '\0';
        strcpy(remote_port, tmp + 1);
    } else {
        strcpy(remote_port, "80");
    }
    strcpy(remote_host, host_port);
}

int Append(char *content, unsigned int *content_length, char *buf) {
    unsigned int length = strlen(buf);
    
    if ((*content_length + length) > MAX_OBJECT_SIZE) {
        return 0;
    }
    
    void *ptr = (void *)((char *)content + *content_length);
    memcpy(ptr, buf, length);
    *content_length = *content_length + length;
    return 1;
}

void get_size(char *buf, unsigned int *size_pointer) {
    if (strstr(buf, "Content-Length")) {
        sscanf(buf, "Content-Length: %d", size_pointer);
    }
}
