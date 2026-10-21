#include "csapp.h"
#include "wrapper.h"

#define DEFAULT_PORT "55556"
#define MAXPORT 6               /* port <= 65535, five digits */

int proxy_verbose = 0;

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

static const char *user_agent_hdr = "\
User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 \
Firefox/10.0.3\r\n";

int get_request_from_client(int connfd, char *request);
int request_and_reply(int connfd, char *request);
int parse_request(char *request, char *hostname, char *newrequest, char *port);
void *thread(void *vargp);

int main(int argc, char *argv[])
{
    struct sockaddr_storage  clientaddr;
    socklen_t                clientlen;
    char                    *port;
    int                      listenfd;

    signal(SIGPIPE, SIG_IGN);

    listenfd = wrap_open_listenfd(port = (argc > 1? argv[1] : DEFAULT_PORT));
    for (;;) {
        VERBOSE_MSG("wait for connection...");
        int connfd;
        pthread_t tid;

        clientlen = sizeof(struct sockaddr_storage);
        connfd = wrap_accept(listenfd, (SA *) &clientaddr, &clientlen);
        if (connfd > 0)
            wrap_pthread_create(&tid, NULL, thread, (void *) connfd);
    }
    wrap_close(listenfd);
    return 0;
}

void *thread(void *vargp)
{
    int connfd = (long) vargp;
    char request[MAXLINE];

    wrap_pthread_detach(pthread_self());

    if (get_request_from_client(connfd, request) > 0)
        request_and_reply(connfd, request);
    wrap_close(connfd);

    return NULL;
}

/*
 * get_request_from_client - Read the first line of the client(connfd), load it
 *              to the request, return connfd. If there is an error, returns
 *              -1 and do nothing.
 */
int get_request_from_client(int connfd, char *request)
{
    rio_t rp;

    rio_readinitb(&rp, connfd);
    if (wrap_rio_readlineb(&rp, request, MAXLINE) < 0)
        return -1;

    return connfd;
}

/*
 * request_and_reply - Establish connection to server and request.
 *         Get reply from the server, then echo it to client(connfd)
 */
int request_and_reply(int connfd, char *request)
{
    int    clientfd;
    char   hostname[MAXLINE], newrequest[MAX_OBJECT_SIZE], port[MAXPORT];
    char   buf[MAX_OBJECT_SIZE];
    rio_t  rp;

    if (parse_request(request, hostname, newrequest, port) < 0) {
        ERR_MSG("wrong request: %s", request);
        return -1;
    }

    clientfd = wrap_open_clientfd(hostname, port);
    if (clientfd < 0) {
        return -1;
    }
    wrap_rio_writen(clientfd, newrequest, strlen(newrequest));
    rio_readinitb(&rp, clientfd);
    while (wrap_rio_readlineb(&rp, buf, MAX_OBJECT_SIZE) > 0) {
        if (wrap_rio_writen(connfd, buf, strlen(buf)) < 0)
            break;
    }
    wrap_close(clientfd);

    return 0;
}

int parse_request(char *request, char *hostname, char *newrequest, char *port)
{
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    sscanf(request, "%s %s %s", method, uri, version);

    /* parse hostname from uri */
    char *p = strstr(uri, "//");
    p = p == NULL? uri : p + 2;

    int c, i;
    for (i = 0;
         (c = *p) != '/' && c != '\0' && c != ':' && i < MAXLINE;
         p++, i++) {
        hostname[i] = c;
    }
    if (i >= MAXLINE) {
        fprintf(stderr, "proxy: out of range on hostname\n");
        return -1;
    }
    hostname[i] = '\0';

    /* get file name */
    char file[MAXLINE];
    if (c == '/') {
        for (i = 0; (c = *p) != '\0' && c != ':' && i < MAXLINE; p++, i++)
            file[i] = c;
        if (i >= MAXLINE) {
            fprintf(stderr, "proxy: out of range on file\n");
            return -1;
        }
        file[i] = '\0';
    } else { /* c == ':' or '\0' */
        file[0] = '/';
        file[1] = '\0';
    }

    /* Get port if it is provided, if not, set it to default(80) */
    if (c == ':')
        strncpy(port, p + 1, MAXPORT);
    else
        strcpy(port, "80");

    if (snprintf(newrequest, MAX_OBJECT_SIZE,
                 "%s %s %s\r\n"
                 "Host: %s\r\n"
                 "%s"
                 "\r\n",
                 method, file, "HTTP/1.0", hostname, user_agent_hdr) < 0) {
        fprintf(stderr, "proxy: out of range on newrequest\n");
        return -1;
    }

    return 0;
}
