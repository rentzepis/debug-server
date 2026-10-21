#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
// static const char *conn_hdr ="Connection: close\r\n";
// static const char *prox_hdr = "Proxy-Connection: close\r\n";

struct Request
{
    char host_name[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
};
void *thread(void *vargp);
void parse_uri(char *uri, struct Request *request);
void doit(int fd);
void clienterror(int fd, char *cause, char *errnumm, char *shortmsg, char *longmsg);
int main(int argc, char **argv)
{

    int listenfd,*connfd;
    pthread_t pid;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE], port[MAXLINE];
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port> \n", argv[0]);
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(clientaddr);
        connfd = Malloc(sizeof(int)); 
        *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s)\n", hostname, port);
        Pthread_create(pid,NULL,thread,connfd);       
    }
    return 0;
}

void *thread(void *vargp) {
    printf("enter thread\n");
    int connfd = *((int*)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void doit(int fd)
{
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE], buf[MAXLINE];
    rio_t client_rio;

    Rio_readinitb(&client_rio, fd);
    Rio_readlineb(&client_rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET"))
    {
        clienterror(fd, method, "501", "Not implemented",
                    "proxy not implement this method");
        return;
    }
    struct Request request;
    parse_uri(uri, &request);
    Rio_readlineb(&client_rio, buf, MAXLINE);
    if (strstr(buf, "Host: ") == uri)
    {
        sscanf(buf, "Host: %s\r\n", request.host_name);
    }
    while (strcmp(buf, "\r\n"))
    {
        Rio_readlineb(&client_rio, buf, MAXLINE);
        printf("%s", buf);
    }

    int server_fd = Open_clientfd(request.host_name, request.port);
    if (server_fd < 0)
    {
        printf("Connect failed");
        return;
    }
    rio_t server_rio;

    char *request_head[MAXLINE];
    sprintf(request_head, "GET %s HTTP/1.0\r\n", request.path);
    sprintf(request_head, "%s\r\n", request_head);
    Rio_writen(server_fd, request_head, strlen(request_head));
    int n;
    rio_readinitb(&server_rio, server_fd);
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)))
    {
        Rio_writen(fd, buf, n);
    }
    Close(server_fd);
}

void parse_uri(char *uri, struct Request *request)
{
    printf("begin to parse\n");
    char *hostpose = strstr(uri, "//");
    if (hostpose == NULL)
    {
        char *pathpose = strstr(uri, "/");
        if (pathpose != NULL)
            strcpy(request->path, pathpose);
        strcpy(request->port, "80");
        return;
    }
    else
    {
        char *portpose = strstr(hostpose + 2, ":");
        if (portpose != NULL)
        {
            int tmp;
            sscanf(portpose + 1, "%d%s", &tmp, request->path);
            sprintf(request->port, "%d", tmp);
            *portpose = '\0';
        }
        else
        {
            char *pathpose = strstr(hostpose + 2, "/");
            if (pathpose != NULL)
            {
                strcpy(request->path, pathpose);
                strcpy(request->port, "80");
                *pathpose = '\0';
            }
        }
        strcpy(request->host_name, hostpose + 2);
    }
    printf("uri: %s %s %s\n",request->host_name,request->port,request->path);
    return;
}
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    sprintf(body, "%s: %s\r\n", errnum, shortmsg);
    sprintf(body, "%s%s: %s\r\n", body, longmsg, cause);

    Rio_writen(fd, body, strlen(body));
}