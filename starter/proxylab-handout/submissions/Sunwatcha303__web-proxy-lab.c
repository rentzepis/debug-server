/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>

#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include "cache.h"
/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20191101 Firefox/63.0.1";

static const char *http_version = "HTTP/1.0";
static const char *connection_close = "close";

typedef struct sockaddr SA;

typedef struct request
{
    char *method;
    char *url;
    const char *http_version;
    char *host;
    const char *connection;
    const char *proxy_connection;
    const char *user_agent;
    char *other;
} request_t;

void handle_client(int connfd);
void serve_content(int connfd, char *host, char *port, char *url, char *buf_request);
void prep_info(char **host, char **port, char *buf_request, request_t *request);
void sigint_handler(int sig);
void *thread(void *vargp);

static int v = 0;
// static int *connfdp = NULL;
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

int main(int argc, char **argv)
{
    init_cache();

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, sigint_handler);

    char *port = "-1";

    int listenfd;
    socklen_t clientlen;

    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    pthread_t tid;

    // printf("%s\n", header_user_agent);
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-V") == 0)
        {
            v = 1;
        }
        else
        {
            int port_num = atoi(argv[i]);
            if (port_num >= (1 << 16))
            {
                fprintf(stderr, "port number is not valid\n");
                exit(0);
            }
            else
            {
                port = argv[i];
            }
        }
    }

    if (strcmp(port, "-1") == 0)
    {
        printf("please enter port number.\n");
        exit(0);
    }
    listenfd = open_listenfd(port);

    if (v)
    {
        printf("proxy listening on port: %s\n", port);
    }
    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        // int *connfdp = Malloc(sizeof(int));
        // *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen);
        int connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (connfd < 0)
        {
            if (v)
            {
                printf("%d\n", connfd);
                fprintf(stderr, "Error: Unable to accept\n");
            }
            // Free(connfdp);
            continue;
        }
        if (getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0))
        {
            fprintf(stderr, "Cannot get client info %s:%s\n", client_hostname, client_port);
            continue;
        }
        if (v)
        {
            printf("Connected to (%s, %s)\n", client_hostname, client_port);
        }
        int *connfdp = Malloc(sizeof(int));
        *connfdp = connfd;
        pthread_create(&tid, NULL, thread, connfdp);
    }

    close_cache();
    return 0;
}

void handle_client(int connfd)
{
    pthread_mutex_lock(&lock);
    size_t n;
    char buf[MAXLINE];
    rio_t rio;
    request_t *request = Malloc(sizeof(request_t));

    request->method = NULL;
    request->url = NULL;
    request->http_version = http_version;
    request->host = NULL;
    request->connection = connection_close;
    request->proxy_connection = connection_close;
    request->user_agent = header_user_agent;
    request->other = NULL;

    rio_readinitb(&rio, connfd);

    while ((n = rio_readlineb(&rio, buf, MAXLINE)) > 0)
    {
        if (v)
        {
            printf("buf %ld bytes: %s", n, buf);
        }

        char *temp = Malloc(n + 1);
        if (temp == NULL)
        {
            fprintf(stderr, "Memory allocation failed\n");
            return;
        }
        strcpy(temp, buf);

        char *split = strtok(temp, " ");

        if (strcmp(split, "\r\n") == 0)
        {
            Free(temp);
            break;
        }
        if (strcmp(split, "User-Agent:") == 0 || strcmp(split, "Connection:") == 0 || strcmp(split, "Proxy-Connection:") == 0)
        {
            Free(temp);
            continue;
        }

        if (strcmp(split, "GET") == 0 || strcmp(split, "POST") == 0 || strcmp(split, "CONNECT") == 0)
        {
            request->method = strdup(split);
            request->url = strdup(strtok(NULL, " "));
        }
        else if (strcmp(split, "Host:") == 0)
        {
            request->host = strdup(strtok(NULL, "\r\n"));
        }
        else
        {
            size_t other_len = (request->other != NULL) ? strlen(request->other) : 0;
            size_t temp_len = strlen(buf);

            char *other = Malloc(other_len + temp_len + 1);
            if (request->other != NULL)
            {
                strcpy(other, request->other);
                Free(request->other);
            }
            else
            {
                other[0] = '\0';
            }
            strcpy(other + other_len, buf);
            other[other_len + temp_len] = '\0';

            request->other = other;
        }

        Free(temp);
    }

    if (v)
    {
        printf("Method: %s\n", request->method);
        printf("URL: %s\n", request->url);
        printf("Http Version: %s\n", request->http_version);
        printf("Host: %s\n", request->host);
        printf("Connection: %s\n", request->connection);
        printf("Proxy Connection: %s\n", request->proxy_connection);
        printf("User-Agent: %s\n", request->user_agent);
        printf("%s\n", request->other);
    }

    if (request->method == NULL)
    {
        pthread_mutex_unlock(&lock);
    }
    else
    {
        block_t *from_cache = find_block(request->url);
        if (from_cache)
        {
            if (v)
            {
                printf("from cache\n");
                print_cache_stats();
            }
            char *response_copy = Malloc(from_cache->response_size);
            memcpy(response_copy, from_cache->response, from_cache->response_size);
            size_t response_size = from_cache->response_size;

            pthread_mutex_unlock(&lock);
            rio_writen(connfd, response_copy, response_size);
            Free(response_copy);
        }
        else
        {
            if (v)
            {
                printf("no data in cache\n");
            }
            char *host = NULL, *port = NULL, *url = NULL;
            char buf_request[MAXBUF];

            url = strdup(request->url);
            prep_info(&host, &port, buf_request, request);
            if (v)
            {
                printf("connecting to %s:%s\n", host, port);
                printf("request: %s\n", buf_request);
            }
            pthread_mutex_unlock(&lock);

            serve_content(connfd, host, port, buf_request, url);
            Free(host);
            Free(port);
            Free(url);
        }
        Free(request->method);
        Free(request->url);
        Free(request->host);
        Free(request->other);
    }
    close(connfd);
    Free(request);
}

void serve_content(int connfd, char *host, char *port, char *buf_request, char *url)
{
    int clientfd;
    unsigned char buf_response[MAXBUF];
    rio_t rio;

    clientfd = open_clientfd(host, port);
    if (clientfd < 0)
    {
        if (v)
        {
            fprintf(stderr, "Error: Unable to connect to %s:%s\n", host, port);
        }
        return;
    }

    rio_readinitb(&rio, clientfd);

    if (rio_writen(clientfd, buf_request, strlen(buf_request)) < 0)
    {
        if (v)
        {
            fprintf(stderr, "Error: Unable to send request\n");
        }
        close(clientfd);
        return;
    }

    unsigned char response[MAX_OBJECT_SIZE];
    size_t response_size = 0;
    ssize_t n;
    while ((n = rio_readnb(&rio, buf_response, MAXBUF)) > 0)
    {
        // printf("buf response: %s\n", buf_response);
        rio_writen(connfd, buf_response, n);

        if ((response_size + n) < MAX_OBJECT_SIZE)
        {
            memcpy(response + response_size, buf_response, n);
        }
        response_size += n;
    }

    if (response_size < MAX_OBJECT_SIZE)
    {
        if (insert_block(url, response, response_size) && v)
        {
            printf("insert to cache succussful\n");
        }
    }
    close(clientfd);
}

void *thread(void *vargp)
{
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    Free(vargp);
    handle_client(connfd);
    close(connfd);
    return NULL;
}

void prep_info(char **host, char **port, char *buf_request, request_t *request)
{
    char *temp_host = strdup(request->host);
    if (temp_host == NULL)
    {
        perror("strdup failed for host");
        return;
    }
    char *token = strtok(temp_host, ":");
    if (token != NULL)
    {
        *host = strdup(token);
        if (*host == NULL)
        {
            perror("strdup failed for host");
            return;
        }

        token = strtok(NULL, "\r\n");
        if (token != NULL)
        {
            *port = strdup(token);
            if (*port == NULL)
            {
                perror("strdup failed for port");
                return;
            }
        }
        else
        {
            *port = strdup("80");
            if (*port == NULL)
            {
                perror("strdup failed for default port");
                return;
            }
        }
    }

    char *path = strstr(request->url, "//");
    path = path ? strchr(path + 2, '/') : NULL;
    if (path)
        path++;

    snprintf(buf_request, MAXBUF,
             "%s /%s %s\r\n"
             "Host: %s\r\n"
             "Connection: %s\r\n"
             "Proxy-Connection: %s\r\n"
             "User-Agent: %s\r\n"
             "%s\r\n",
             request->method, path ? path : "", request->http_version,
             request->host,
             request->connection, request->proxy_connection,
             request->user_agent, request->other);

    if (v)
    {
        printf("request:\n%s", buf_request);
    }

    free(temp_host);
}

void sigint_handler(int sig)
{
    close_cache();
    // Free(connfdp);
    exit(0);
}