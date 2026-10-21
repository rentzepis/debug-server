/*
 * Name: Zhang Liheng
 * ID: 2200013214
 *
 * This is a proxy server that can handle http requests and cache objects.
 * Every single connection is handled by a thread, which reads a
 * request line, determine if it is a request, and direct the `doit`
 * routine (refer to a tiny web server) to handle the rest. The `doit`
 * routine will read request headers and search the cache for a
 * previous response from the server.
 * If a cached response is found, the routine will directly forward
 * it to the client.
 * Otherwise it will send the client's request to the web server,
 * read the response from web server, forward it to the client,
 * and store it in the cache.
 */

#include "csapp.h"
#include "cache.h"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

extern int init_cache(void);
extern int search_cache(char url[MAXLINE], char buf[MAX_OBJECT_SIZE]);
extern void store_cache(char url[MAXLINE],
                        char buf[MAX_OBJECT_SIZE], int length);
extern void destroy_cache(void);

void *thread(void *_clientfd);
void doit(rio_t *client_rp, int clientfd, char *url);
int parse_url(char *url, char *server_host, char *server_port, char *uri);
int read_requesthdrs(rio_t *client_rp, char *result, char *server_host);

/*
 * main - main thread
 */
int main(int argc, char *argv[])
{
    // If we write a socket that has received an RST, the kernel will send
    // a SIGPIPE signal to the process, which will cause the process to
    // terminate. We can ignore the signal to prevent the process from
    // terminating.
    Signal(SIGPIPE, SIG_IGN);

    int listenfd, clientfd;
    char client_host[MAXLINE], client_port[MAXLINE];
    socklen_t client_len;
    struct sockaddr_storage client_addr;
    pthread_t tid;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    if (init_cache() == -1)
    {
        fprintf(stderr, "init_cache error\n");
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    client_len = sizeof(client_addr);

    while (1)
    {
        clientfd = accept(listenfd, (SA *)&client_addr, &client_len);
        if (clientfd < 0)
        {
            fprintf(stderr, "accept error: %s\n", strerror(errno));
            continue;
        }
        if (getnameinfo((SA *)&client_addr, client_len,
                        client_host, MAXLINE, client_port, MAXLINE, 0) < 0)
        {
            fprintf(stderr, "getnameinfo error: %s\n", strerror(errno));
            continue;
        }
        Pthread_create(&tid, NULL, thread, (void *)(long)clientfd);
    }

    destroy_cache();

    Close(listenfd);

    return 0;
}

/*
 * thread - read a request line, determine if it is a GET request,
 * and direct doit routine (refer to tiny web server) to handle the rest.
 * return (void *)0 on success, (void *)-1 on error.
 */
void *thread(void *_clientfd)
{
    if (pthread_detach(pthread_self()) < 0)
    {
        fprintf(stderr, "pthread_detach error: %s\n", strerror(errno));
        return (void *)-1;
    }

    int clientfd = (int)(long)_clientfd;
    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    rio_t client_rio;

    // read request from client
    rio_readinitb(&client_rio, clientfd);
    if (rio_readlineb(&client_rio, buf, MAXLINE) <= 0)
    {
        fprintf(stderr, "rio_readlineb error\n");
        return (void *)-1;
    }

    if (sscanf(buf, "%s%s%s", method, url, version) != 3)
    {
        fprintf(stderr, "malformed request line\n");
        return (void *)-1;
    }

    if (!strcmp(method, "GET"))
        doit(&client_rio, clientfd, url);
    else
        fprintf(stderr, "unknown request method\n");

    Close(clientfd);

    return (void *)0;
}

/*
 * doit - reads request headers, search the cache for fit, send
 * client request to web server, read response from web server,
 * forward it to client, and store it in the cache.
 */
void doit(rio_t *client_rp, int clientfd, char *url)
{
    char buf[MAXLINE], header[MAXLINE], cache_buf[MAX_OBJECT_SIZE];
    char server_host[MAXLINE], server_port[MAXLINE], uri[MAXLINE];
    rio_t server_rio;
    int serverfd, length, object_length;

    if (read_requesthdrs(client_rp, header, server_host) < 0)
    {
        fprintf(stderr, "malformed request header\n");
        return;
    }

    // search cache
    if ((length = search_cache(url, cache_buf)) != -1) // cache hit
    {
        if (rio_writen(clientfd, cache_buf, length) != length)
        {
            fprintf(stderr, "rio_writen error\n");
        }
        return;
    }
    // cache miss

    if (parse_url(url, server_host, server_port, uri) < 0)
    {
        fprintf(stderr, "malformed URL\n");
        return;
    }

    serverfd = open_clientfd(server_host, server_port);
    if (serverfd < 0)
    {
        fprintf(stderr, "open_clientfd error: %s\n", strerror(errno));
        return;
    }

    // send request to web server
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-overflow"
    sprintf(buf, "GET %s HTTP/1.0\r\n%s", uri, header);
#pragma GCC diagnostic pop
    if (rio_writen(serverfd, buf, strlen(buf)) != strlen(buf))
    {
        fprintf(stderr, "rio_writen error\n");
        return;
    }

    // read content from web server
    rio_readinitb(&server_rio, serverfd);
    while ((length = rio_readlineb(&server_rio, buf, MAXLINE)))
    {
        if (length == -1)
        {
            fprintf(stderr, "rio_readlineb error\n");
            return;
        }

        if (object_length + length > MAX_OBJECT_SIZE) // object too large
            object_length = -1;

        if (object_length != -1)
        {
            memcpy(cache_buf + object_length, buf, length);
            object_length += length;
        }

        // send content to client
        if (rio_writen(clientfd, buf, length) != length)
        {
            fprintf(stderr, "rio_writen error\n");
            return;
        }
    }

    // store content in cache
    if (object_length != -1)
        store_cache(url, cache_buf, object_length);

    Close(serverfd);
}

/*
 * parse_url - parse URL into server_host, server_port and uri fields.
 * return 0 on success, -1 on error.
 */
int parse_url(char *url, char *server_host, char *server_port, char *uri)
{
    if (strncmp(url, "http://", strlen("http://")))
        return -1;

    char temp_url[MAXLINE];
    char *port_start, *uri_start;
    int len = strlen("http://");
    strcpy(temp_url, url);

    // derive URI
    uri_start = strchr(temp_url + len, '/');
    if (!uri_start)
        return -1;
    strcpy(uri, uri_start);
    *uri_start = '\0'; // mark the end of port/hostname

    // derive port
    port_start = strchr(temp_url + len, ':');
    if (port_start)
    {
        strcpy(server_port, port_start + 1);
        *port_start = '\0'; // mark the end of hostname
    }
    else
    {
        strcpy(server_port, "80");
    }

    // derive hostname
    strcpy(server_host, temp_url + len);

    return 0;
}

/*
 * read_requesthdrs - read request headers from client (refer to TINY)
 * return 0 on success, -1 on error.
 */
int read_requesthdrs(rio_t *client_rp, char *req_header_buf, char *server_host)
{
    char buf[MAXLINE];
    char hdr_host[MAXLINE] = {};
    req_header_buf[0] = '\0';

    rio_readlineb(client_rp, buf, MAXLINE);
    if (!strncmp(buf, "Host", strlen("Host")))
        strcpy(hdr_host, buf + strlen("Host: "));
    else if (strncmp(buf, "User-Agent", strlen("User-Agent")) &&
             strncmp(buf, "Connection", strlen("Connection")) &&
             strncmp(buf, "Proxy-Connection", strlen("Proxy-Connection")))
    {
        strcat(req_header_buf, buf);
    }
    while (strcmp(buf, "\r\n"))
    {
        rio_readlineb(client_rp, buf, MAXLINE);
        if (!strncmp(buf, "Host", strlen("Host")))
            strcpy(hdr_host, buf + strlen("Host: "));
        else if (strncmp(buf, "User-Agent", strlen("User-Agent")) &&
                 strncmp(buf, "Connection", strlen("Connection")) &&
                 strncmp(buf, "Proxy-Connection", strlen("Proxy-Connection")))
        {
            strcat(req_header_buf, buf);
        }
    }

    sprintf(req_header_buf + strlen(req_header_buf), "%s%s%s%s%s%s",
            "Host: ", *hdr_host ? hdr_host : server_host,
            *hdr_host ? "" : "\r\n", user_agent_hdr,
            "Connection: close\r\n", "Proxy-Connection: close\r\n\r\n");

    return 0;
}
