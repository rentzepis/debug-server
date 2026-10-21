#include <stdio.h>
#include "csapp.h"
#include "cache.h"

struct req_line
{
    char method[MAXLINE];
    char host[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
};

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

/* Thread routine. */
void *forward_thread(void *vargp);

/* Forward the request from the client then send back the response. */
void forward(int clientfd);

CacheNode *cache;

/* Parse request line to struct req_line_t. */
struct req_line parse_req_line(char *req_line);

int main(int argc, char *argv[])
{
    int listenfd;
    int *clientfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Initiate cache. */
    cache = init_cache();

    /* Ignore SIGPIPE. */
    Signal(SIGPIPE, SIG_IGN);

    /* Check the port argument. */
    if (argc < 1)
        exit(1);

    /* Open the proxy server. */
    listenfd = Open_listenfd(argv[1]);

    while (1)
    {
        clientlen = sizeof(clientaddr);
        clientfdp = malloc(sizeof(int));

        /* Accept a client. */
        if ((*clientfdp = accept(listenfd, (SA *)&clientaddr, &clientlen)) < 0)
            continue;

        /* Create a new thread. */
        pthread_create(&tid, NULL, forward_thread, clientfdp);
    }

    /* Destruct the cache. */
    destruct_cache(cache);

    return 0;
}

void *forward_thread(void *vargp)
{
    int clientfd;

    clientfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    free(vargp);

    /* Forward the request and response. */
    forward(clientfd);

    /* Close the connection. */
    close(clientfd);
}

void forward(int clientfd)
{
    rio_t rio;
    size_t n;
    struct req_line req_line;
    char buf[MAXLINE], cache_buf[MAX_OBJECT_SIZE];
    char req_line_s[MAXLINE], headers[MAXLINE], url[MAXLINE];
    int hdr_host = 0, hdr_user_agent = 0, hdr_conn = 0, hdr_proxy_conn = 0;
    int hostfd;
    CacheNode *node;

    Rio_readinitb(&rio, clientfd);

    /* Get request line. */
    if (rio_readlineb(&rio, buf, MAXLINE) < 0)
        return;

    /* Get URL from the request line. */
    sscanf(buf, "%*s %s %*s", url);

    /* Parse the request line. */
    req_line = parse_req_line(buf);

    /* Only GET method is allowed. */
    if (strcasecmp(req_line.method, "GET"))
        return;

    /* Cache hit. */
    if ((node = find(url, cache)) != NULL)
    {
        to_front(url, cache);
        rio_writen(clientfd, node->data, node->size);
        return;
    }

    /* Clean the headers. */
    memset(headers, 0, MAXLINE);

    /* Read and modify the headers provided by the client. */
    Rio_readlineb(&rio, buf, MAXLINE);
    while (strcmp(buf, "\r\n"))
    {
        /* If Host header is provided, just use it. */
        if (!strncasecmp("Host:", buf, strlen("Host:")))
            hdr_host = 1;

        /* If User-Agent header is provided, ignore and use default value. */
        if (!strncasecmp("User-Agent:", buf, strlen("User-Agent:")))
        {
            hdr_user_agent = 1;
            strcpy(buf, user_agent_hdr);
        }

        /* If Connection header is provided, ignore and use default value. */
        if (!strncasecmp("Connection:", buf, strlen("Connection:")))
        {
            hdr_conn = 1;
            strcpy(buf, connection_hdr);
        }

        /* If Proxy-Connection header is provided, ignore and use default value. */
        if (!strncasecmp("Proxy-Connection:", buf, strlen("Proxy-Connection:")))
        {
            hdr_proxy_conn = 1;
            strcpy(buf, proxy_connection_hdr);
        }

        sprintf(headers, "%s%s", headers, buf);
        Rio_readlineb(&rio, buf, MAXLINE);
    }

    /* Include necessary headers, if not set. */
    if (!hdr_host)
    {
        if (!strcmp(req_line.port, ""))
            sprintf(headers, "%sHost: %s\r\n", headers, req_line.host);
        else
            sprintf(headers, "%sHost: %s:%s\r\n", headers, req_line.host, req_line.port);
    }
    if (!hdr_user_agent)
        sprintf(headers, "%s%s", headers, user_agent_hdr);
    if (!hdr_conn)
        sprintf(headers, "%s%s", headers, connection_hdr);
    if (!hdr_proxy_conn)
        sprintf(headers, "%s%s", headers, proxy_connection_hdr);

    /* The last line should be "\r\n". */
    sprintf(headers, "%s\r\n", headers);

    /* Generate request line string. */
    sprintf(req_line_s, "GET %s HTTP/1.0\r\n", req_line.path);

    /* Connect to the target server. */
    if ((hostfd = open_clientfd(req_line.host, (!strcmp(req_line.port, "") ? "80" : req_line.port))) < 0)
        return;

    /* Send the HTTP request message to the target server. */
    sprintf(buf, "%s%s", req_line_s, headers);
    if ((rio_writen(hostfd, buf, strlen(buf))) != strlen(buf))
        return;

    /* Receive message from target server and forward it to the client. */
    Rio_readinitb(&rio, hostfd);
    memset(cache_buf, 0, MAX_OBJECT_SIZE);
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0)
    {
        /* Store in the cache buffer. */
        sprintf(cache_buf, "%s%s", cache_buf, buf);

        /* Send to the client. */
        if (rio_writen(clientfd, buf, n) != n)
            break;
    }

    cache_obj(url, cache_buf, strlen(cache_buf), cache);
}

struct req_line parse_req_line(char *req_line)
{
    struct req_line res;
    char uri[MAXLINE], buf[MAXLINE];

    /* Clean the result. */
    memset(&res, 0, sizeof(res));

    /* Parse from request line: METHOD URI VERSION */
    sscanf(req_line, "%s %s %*s", res.method, uri);

    /* Parse from URI: PROTOCOL://HOST:PORT/PATH */
    sscanf(uri, "%*[^:]://%[^/]%s", buf, res.path);
    sscanf(buf, "%[^:]:%s", res.host, res.port);

    return res;
}