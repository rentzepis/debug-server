#include <stdbool.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* HTTP prefix */
#define HTTP_PREFIX "http://"

/* Default http port */
#define HTTP_PORT "80"

/* End line string defined in HTTP */
#define CRLF "\r\n"

/* Maximum number of headers allowed */
#define MAX_NUM_HEADERS 1024

/* Number of prethreaded workers */
#define NUM_THREADS 2
/* Number of slots in sbuf */
#define SBUF_SIZE 8

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

static sbuf_t sbuf;
static lru_t lru;

void *thread(void *vargp);
void doit(int fd);
int clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int read_req_line(rio_t *rio, int fd, char *method, char *uri, char *domain, char *host, char *port, char *path);
int process_req(rio_t *rio, int clientfd, int fd, char *buf, char *path, char *domain);

int main(int argc, char *argv[])
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    pthread_t tid;

    /* Handle the SIGPIPE error in read/write instead of SIGPIPE handler */
    signal(SIGPIPE, SIG_IGN);

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    sbuf_init(&sbuf, SBUF_SIZE);
    for (int i = 0; i < NUM_THREADS; i++)
        Pthread_create(&tid, NULL, thread, NULL);

    lru_init(&lru, MAX_CACHE_SIZE);

    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (connfd < 0)
            continue;
        sbuf_enque(&sbuf, connfd);
    }

    return 0;
}

void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    while (1)
    {
        int connfd = sbuf_deque(&sbuf);
        doit(connfd);
        Close(connfd);
    }
}

void doit(int fd)
{
    rio_t rio;
    char buf[MAXLINE], method[MAXLINE], domain[MAXLINE], host[MAXLINE], port[MAXLINE], path[MAXLINE];
    char *uri;

    int resp_size;
    char *resp;

    int clientfd;
    int errcode;
    int rc, num_filled, last_rc;

    if ((uri = Malloc(MAXLINE)) == NULL)
    {
        clienterror(fd, "service error", "500", "OOM", "Insufficient memory for uri allocation");
        return;
    }

    /* Read request line */
    Rio_readinitb(&rio, fd);
    errcode = read_req_line(&rio, fd, method, uri, domain, host, port, path);
    switch (errcode)
    {
    case -1:
        return;
    case -2:
        clienterror(fd, "request line", "400", "Malformed Request Line", "The request line format is invalid");
        return;
    case -3:
        clienterror(fd, method, "501", "Not Implemented", "Proxy only supports GET");
        return;
    case -4:
        clienterror(fd, uri, "400", "Malformed URI", "The uri is not a valid http uri");
        return;
    }

    /* Look up in cache */
    if ((resp = lru_get(&lru, uri, &resp_size)) != NULL)
    {
        Rio_writen(fd, resp, resp_size);
        return;
    }

    /* Cache miss and prepare to store response in cache */
    if ((resp = Malloc(MAX_OBJECT_SIZE)) == NULL)
    {
        clienterror(fd, "service error", "500", "OOM", "Insufficient memory for response allocation");
        return;
    }

    /* Open connection to server */
    if ((clientfd = Open_clientfd(host, port)) < 0)
    {
        clienterror(fd, domain, "502", "Connection Failed", "Cannot connect to upstream server");
        return;
    }

    /* Read & Send requet */
    errcode = process_req(&rio, clientfd, fd, buf, path, domain);
    switch (errcode)
    {
    case -1:
        return;
    case -2:
        clienterror(fd, domain, "502", "Connection Closed", "Connection to upstream server closed");
        Close(clientfd);
        return;
    case -3:
        clienterror(fd, buf, "400", "Malformed Header", "The header format is invalid");
        Close(clientfd);
        return;
    case -4:
        clienterror(fd, "", "431", "Max Header Exceeded", "The number of headers exceeds the maximum allowed");
        Close(clientfd);
        return;
    case -5:
        clienterror(fd, "", "400", "Malformed Header", "The header section does not end with CRLF");
        Close(clientfd);
        return;
    }

    /* Forward response */
    Rio_readinitb(&rio, clientfd);
    num_filled = 0;
    while ((rc = Rio_readnb(&rio, resp, MAX_OBJECT_SIZE)) > 0)
    {
        Rio_writen(fd, resp, rc);
        num_filled++;
        last_rc = rc;
    }
    if (rc == 0 && num_filled == 1)
    {
        if (lru_put(&lru, uri, resp, last_rc) < 0)
            app_errorf("OOM when caching response");
    }

    /* Close connection to server */
    Close(clientfd);
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
int clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    if (Rio_writen(fd, buf, strlen(buf)) < 0)
        return -1;
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    if (Rio_writen(fd, buf, strlen(buf)) < 0)
        return -1;

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    if (Rio_writen(fd, buf, strlen(buf)) < 0)
        return -1;
    sprintf(buf, "<body bgcolor="
                 "ffffff"
                 ">\r\n");
    if (Rio_writen(fd, buf, strlen(buf)) < 0)
        return -1;
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    if (Rio_writen(fd, buf, strlen(buf)) < 0)
        return -1;
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    if (Rio_writen(fd, buf, strlen(buf)) < 0)
        return -1;
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    if (Rio_writen(fd, buf, strlen(buf)) < 0)
        return -1;

    return 0;
}
/* $end clienterror */

/* Return error code:
-1: Error read from client
-2: The request line format is invalid
-3: Proxy only supports GET
-4: The uri is not a valid http uri
*/
int read_req_line(rio_t *rio, int fd, char *method, char *uri, char *domain, char *host, char *port, char *path)
{
    char buf[MAXLINE], version[MAXLINE];
    int num_uri_tokens;

    if (Rio_readlineb(rio, buf, MAXLINE) < 0)
        return -1;

    if ((sscanf(buf, "%s %s %s\r\n", method, uri, version)) != 3)
        return -2;
    if (strcasecmp(method, "GET"))
        return -3;

    num_uri_tokens = sscanf(uri, "http://%[^/]%s", domain, path);
    if (num_uri_tokens < 1)
        return -4;
    if (num_uri_tokens == 1)
        strcpy(path, "/");
    if (sscanf(domain, "%[^:]:%s", host, port) < 2)
    {
        strcpy(host, domain);
        strcpy(port, HTTP_PORT);
    }

    return 0;
}

/* Return error code:
-1: Error read from client
-2: Connection to upstream server closed
-3: The header format is invalid
-4: Maximum number of headers exceeded
-5: Header section not ended with CRLF
*/
int process_req(rio_t *rio, int clientfd, int fd, char *buf, char *path, char *domain)
{
    char hdrkey[MAXLINE];
    int num_header_tokens, rc, num_headers = 0;
    bool end_with_crlf = false, has_host = false;

    /* Send request line */
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    if (Rio_writen(clientfd, buf, strlen(buf)) < 0)
        return -2;

    /* Read & send headers */
    while ((rc = Rio_readlineb(rio, buf, MAXLINE)) > 0)
    {
        if (!strcmp(buf, CRLF))
        {
            end_with_crlf = true;
            break;
        }

        if (++num_headers > MAX_NUM_HEADERS)
            return -4;

        if ((num_header_tokens = sscanf(buf, "%[^:]:", hdrkey)) < 1)
            return -3;

        if (!strcasecmp(hdrkey, "User-Agent") || !strcasecmp(hdrkey, "Connection") || !strcasecmp(hdrkey, "Proxy-Connection"))
            continue;
        else if (!strcasecmp(hdrkey, "Host"))
            has_host = true;

        if (Rio_writen(clientfd, buf, strlen(buf)) < 0)
            return -2;
    }

    if (rc < 0)
        return -1;
    if (!end_with_crlf)
        return -5;

    if (Rio_writen(clientfd, (void *)user_agent_hdr, strlen(user_agent_hdr)) < 0)
        return -2;
    if (Rio_writen(clientfd, (void *)connection_hdr, strlen(connection_hdr)) < 0)
        return -2;
    if (Rio_writen(clientfd, (void *)proxy_connection_hdr, strlen(proxy_connection_hdr)) < 0)
        return -2;
    if (!has_host)
    {
        sprintf(buf, "Host: %s", domain);
        if (Rio_writen(clientfd, buf, strlen(buf)) < 0)
            return -2;
    }

    /* Send request */
    if (Rio_writen(clientfd, CRLF, strlen(CRLF)) < 0)
        return -2;

    return 0;
}