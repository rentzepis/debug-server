#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"

#define METHOD_LEN 25
#define VERSION_LEN 15
#define PORT_LEN 25
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static const char *server_version = "HTTP/1.0";
static const char *default_port = "80";

struct Request {
    char request[MAXLINE];
    char method[METHOD_LEN];
    char host_addr[MAXLINE];
    char port[PORT_LEN];
    char path[MAXLINE];
    char version[VERSION_LEN];
};

int parse_request(rio_t *rio, char *request, struct Request *req);
int from_client_to_server(rio_t *rio, struct Request *req, int serverfd, int clientfd);
int from_server_to_client(int serverfd, int clientfd, char *response);
void *handle_client(void *vargp);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    Signal(SIGPIPE, SIG_IGN);
    cache_init();

    int listenfd = Open_listenfd(argv[1]);
    struct sockaddr_storage clientaddr;

    while (1) {
        socklen_t clientlen = sizeof(clientaddr);
        int clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        
        pthread_t tid;
        Pthread_create(&tid, NULL, handle_client, (void *)(long)clientfd);
    }
}

void *handle_client(void *vargp) {
    Pthread_detach(Pthread_self());
    int clientfd = (int)(long)vargp;
    rio_t rio_to_client;
    Rio_readinitb(&rio_to_client, clientfd);

    struct Request req;
    memset(&req, 0, sizeof(struct Request));

    char request[MAXLINE];
    if (parse_request(&rio_to_client, request, &req) < 0) {
        Close(clientfd);
        return NULL;
    }

    char cached_obj[MAX_OBJECT_SIZE];
    ssize_t n = read_cache(req.request, cached_obj);
    if (n > 0) {
        if (rio_writen(clientfd, cached_obj, n) < 0) {
            fprintf(stderr, "Error writing cache object to clientfd\n");
        }
        Close(clientfd);
        return NULL;
    }

    int serverfd = Open_clientfd(req.host_addr, req.port);
    if (from_client_to_server(&rio_to_client, &req, serverfd, clientfd) < 0) {
        Close(serverfd);
        Close(clientfd);
        return NULL;
    }

    char response[MAX_OBJECT_SIZE];
    int response_size = from_server_to_client(serverfd, clientfd, response);

    if (response_size > 0 && response_size < MAX_OBJECT_SIZE) {
        write_cache(req.request, response, response_size);
    }

    Close(serverfd);
    Close(clientfd);
    return NULL;
}

int parse_request(rio_t *rio, char *request, struct Request *req) {
    char uri[MAXLINE];
    char version[VERSION_LEN];
    char method[METHOD_LEN];

    if (Rio_readlineb(rio, request, MAXLINE) <= 0) {
        fprintf(stderr, "Error reading request line\n");
        return -1;
    }

    if (sscanf(request, "%s %s %s", method, uri, version) != 3) {
        return -1;
    }

    strncpy(req->request, request, MAXLINE - 1);
    strncpy(req->method, method, METHOD_LEN - 1);
    strncpy(req->version, version, VERSION_LEN - 1);

    char hostname_port[MAXLINE];
    char path[MAXLINE];
    char port[PORT_LEN];
    char hostname[MAXLINE];

    char *temp = strstr(uri, "://");
    if (temp == NULL) {
        return -1;
    }
    temp += 3;

    if (sscanf(temp, "%[^/]%s", hostname_port, path) != 2) {
        return -1;
    }

    if (sscanf(hostname_port, "%[^:]:%s", hostname, port) == 2) {
        strncpy(req->port, port, PORT_LEN - 1);
    } else {
        strncpy(req->port, default_port, PORT_LEN - 1);
    }

    strncpy(req->host_addr, hostname, MAXLINE - 1);
    strncpy(req->path, path, MAXLINE - 1);

    return 0;
}

int from_client_to_server(rio_t *rio, struct Request *req, int serverfd, int clientfd) {
    if (strcasecmp(req->method, "GET") != 0) {
        return -1;
    }

    char buf[MAXLINE];
    char *requestheaders[MAXLINE];
    int m = 0;
    ssize_t n;

    while ((n = Rio_readlineb(rio, buf, MAXLINE)) > 0) {
        if (!strcmp(buf, "\r\n")) {
            break;
        }
        requestheaders[m++] = strdup(buf);
    }

    char formatted_request[MAXLINE];
    n = snprintf(formatted_request, MAXLINE, "%s %s %s\r\n", req->method, req->path[0] ? req->path : "/", server_version);

    if (rio_writen(serverfd, formatted_request, n) < 0) {
        fprintf(stderr, "Error writing request to server\n");
        return -1;
    }

    int has_host_hdr = 0, has_agent_hdr = 0, has_conn_hdr = 0, has_proxy_conn_hdr = 0;
    char host_hdr[MAXLINE];
    snprintf(host_hdr, MAXLINE, "Host: %s\r\n", req->host_addr);

    for (int i = 0; i < m; ++i) {
        if (strncmp(requestheaders[i], "User-Agent:", 11) == 0) {
            has_agent_hdr = 1;
            rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));
        } else if (strncmp(requestheaders[i], "Connection:", 11) == 0) {
            has_conn_hdr = 1;
            rio_writen(serverfd, connection_hdr, strlen(connection_hdr));
        } else if (strncmp(requestheaders[i], "Proxy-Connection:", 17) == 0) {
            has_proxy_conn_hdr = 1;
            rio_writen(serverfd, proxy_connection_hdr, strlen(proxy_connection_hdr));
        } else if (strncmp(requestheaders[i], "Host:", 5) == 0) {
            has_host_hdr = 1;
            rio_writen(serverfd, host_hdr, strlen(host_hdr));
        } else {
            rio_writen(serverfd, requestheaders[i], strlen(requestheaders[i]));
        }
        free(requestheaders[i]);
    }

    if (!has_agent_hdr) rio_writen(serverfd, user_agent_hdr, strlen(user_agent_hdr));
    if (!has_conn_hdr) rio_writen(serverfd, connection_hdr, strlen(connection_hdr));
    if (!has_proxy_conn_hdr) rio_writen(serverfd, proxy_connection_hdr, strlen(proxy_connection_hdr));
    if (!has_host_hdr) rio_writen(serverfd, host_hdr, strlen(host_hdr));

    rio_writen(serverfd, "\r\n", 2);
    return 0;
}

int from_server_to_client(int serverfd, int clientfd, char *response) {
    char buf[MAXLINE];
    int response_size = 0;
    ssize_t n;

    while ((n = rio_readn(serverfd, buf, MAXLINE)) > 0) {
        if (response_size + n <= MAX_OBJECT_SIZE) {
            memcpy(response + response_size, buf, n);
            response_size += n;
        }
        if (rio_writen(clientfd, buf, n) < 0) {
            return -1;
        }
    }

    return response_size;
}