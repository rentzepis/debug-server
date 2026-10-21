/*
 * proxy.c - Concurrent proxy with cache
 * Andrew ID1: sgoyal
 * Andrew ID2: pchun1
 */

#include <string.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including these long lines in your code */
static char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static char *connection_hdr = "Connection: close\r\n";
static char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
static char *version = "HTTP/1.0\r\n";

static struct cache_header *cache;
void call_server(int fd);
void* thread(void* connfd);
void clienterror(int fd, char *cause, char *errnum,
         char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
    int listenfd, port, clientlen;
    int* connfd;
    struct sockaddr_in clientaddr;
    pthread_t tid;

    signal(SIGPIPE, SIG_IGN);

    cache = cache_init();

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    port = atoi(argv[1]);
    listenfd = Open_listenfd(port);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = (int *)malloc(sizeof(int));
        *connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *)&clientlen);
        Pthread_create(&tid, NULL, thread, (void *)connfd);
    }
}

void read_requesthdrs(rio_t *rio_client, rio_t *rio_server, char *host)
{
    char buf[MAXLINE], header[MAXLINE];

    Rio_readlineb(rio_client, buf, MAXLINE);
    if (!strcasecmp(header,"Host"))
        sprintf(buf,"Host: %s", host);
    Rio_writen(rio_server->rio_fd,buf,strlen(buf));

    while(strcmp(buf, "\r\n")) {

        Rio_readlineb(rio_client, buf, MAXLINE);
        sscanf(buf,"%s:", header);

        if (strcasecmp(header,"Accept:") == 0){
            Rio_writen(rio_server->rio_fd,accept_hdr,strlen(accept_hdr));

        } else if (strcasecmp(header,"User-Agent:")==0){
            Rio_writen(rio_server->rio_fd,user_agent_hdr,strlen(user_agent_hdr));

        } else if (strcasecmp(header,"Accept-Encoding:")==0){
            Rio_writen(rio_server->rio_fd,accept_encoding_hdr,strlen(accept_encoding_hdr));

        } else if (strcasecmp(header,"Connection:")==0){
            Rio_writen(rio_server->rio_fd,connection_hdr,strlen(connection_hdr));

        } else if (strcasecmp(header,"Proxy-Connection:")==0){
            Rio_writen(rio_server->rio_fd,proxy_connection_hdr,strlen(proxy_connection_hdr));

        } else {
            Rio_writen(rio_server->rio_fd,buf,strlen(buf));
        }

    }
    Rio_writen(rio_server->rio_fd,"\r\n",strlen(buf));
    return;
}


int read_request_line(char *buf, char *method, char *host, char* path, int* port) {

    char dont_need[MAXLINE];
    char uri[MAXLINE];
    char* placeholder;
    char* temp;

    if (strchr(buf, '/') == NULL || strlen(buf) <= 0) {
        return -1; // Check for valid request line
    }

    sscanf(buf, "%s %s %s", method, uri, dont_need);
    placeholder = strstr(uri,"://");
    placeholder += 3;

    //Set defaults
    path[0] = '/';
    path[1] = 0;
    *port = 80;

    //Check if there is a port
    if ((temp = strchr(placeholder,':')) != NULL && strchr(placeholder, '/') > temp){ //Port number is given
        strncpy(host,placeholder,strchr(placeholder,':')-placeholder);
        placeholder = strchr( placeholder,':' );
        *port = atoi( ++placeholder );
    } else {                      // No port specified
        strncpy(host,placeholder,strchr(placeholder,'/')-placeholder);
    }

    if (strchr(placeholder,'/') != NULL){ //There is a path
        strcpy(path,strchr(placeholder,'/'));
    }

    return 0;
}

void call_server(int client_fd){
    rio_t rio_client;
    char method[MAXLINE], host[MAXLINE], path[MAXLINE],  buf[MAXLINE], dont_need[MAXLINE];
    struct cache_block* block_entry;
    Rio_readinitb(&rio_client, client_fd);
    Rio_readlineb(&rio_client, buf, MAXLINE);
    int port;

    sscanf(buf, "%s %s", method, dont_need);

    if (strlen(method) == 0 || strlen(dont_need) == 0) {
        printf("Empty request\n");
        return;
    }
    if (strcasecmp(method,"GET") != 0) {
        clienterror(client_fd, "HTTP/1.0", "501",
         "HTTPS unsupported", "Sorry but HTTPS is current unsupported.");
        printf("Not a GET request\n");
        return;
    }
    read_request_line(buf,method,host,path, &port);
    char* tag = malloc(strlen(host) + strlen(path) + 1);
    strcat(strcpy(tag, host), path);
    block_entry = find_in_cache(cache,tag);
    if (block_entry != NULL ){
        Rio_writen(client_fd, block_entry->data, block_entry->size);
    } else {
        rio_t  rio_server;
        int server_fd, n;
        char data[MAX_OBJECT_SIZE], requestline[MAXLINE];
        int dataSize = 0;

        server_fd = Open_clientfd_r(host,port);
        if (server_fd < 0) {
            clienterror(client_fd, "HTTP/1.0", "404",
         "Website not found", "Website not found man. ):");
            dns_error("unable to connect to server");
            return;
        }
        sprintf(requestline,"%s %s %s", method, path, version);

        Rio_writen(server_fd,requestline,strlen(requestline));
        Rio_readinitb(&rio_server, server_fd);
        read_requesthdrs(&rio_client,&rio_server,host);

        while((n = Rio_readnb(&rio_server, buf, MAXLINE)) > 0) {
            Rio_writen(client_fd, buf, n);

            if (dataSize +n < MAX_OBJECT_SIZE) memcpy(data+dataSize, buf, n);
            dataSize += n;
        }
        if (dataSize < MAX_OBJECT_SIZE && strstr(tag, "nfl") == NULL) insert_into_cache(cache, dataSize, data, tag);
        free(tag);
        Close(server_fd);
    }

}

void *thread(void *connfd){
    int client_fd = *(int *)connfd;
    free(connfd);

    Pthread_detach(pthread_self());
    call_server(client_fd);

    Close(client_fd);
    return NULL;
}

void clienterror(int fd, char *cause, char *errnum,
         char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html>\r\n<head>\r\n");
    sprintf(body, "%s<title>Shashank and Paul's Awesome Proxy</title>\r\n", body);
    sprintf(body, "%s<link href=""http://www.paulchun.com/proxy.css"" rel=""stylesheet"">\r\n", body);
    sprintf(body, "%s</head>\r\n<body>\r\n<center>\r\n<br><br><br>\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><h1>Shashank and Paul's Awesome Proxy</h1>\r\n", body);
    sprintf(body, "%s</center>\r\n</body>\r\n</html>", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
