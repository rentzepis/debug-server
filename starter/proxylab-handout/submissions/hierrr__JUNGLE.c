#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE (1049000)
#define MAX_OBJECT_SIZE (102400)
#define MAX_HASH_TABLE_SIZE (1<<16)

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *request_line_format = "GET %s HTTP/1.0\r\n";
static const char *host_hdr_format = "HOST: %s\r\n";

void process(int fd);
void parse_uri(char *uri, char *hostname, int *port, char *path);
void set_http_request_hdr(char *http_hdr, char *hostname, int *port, char *path, rio_t *rio);
int connect_to_server(char *hostname, int port);

void *thread_main(void *targs);
unsigned int get_hash_key(char *string);
void set_table_entry(unsigned int hash_key);

typedef struct cached_data
{
    char is_used;
    char data[MAX_OBJECT_SIZE];
} cached_data_t;

cached_data_t cache_table[MAX_HASH_TABLE_SIZE] = { 0 };

int main(int argc, char **argv)
{
    char hostname[MAXLINE], port[MAXLINE];
    int listenfd;
    int connfd; // client >> proxy
    socklen_t clientlen;
    struct sockaddr_storage client_addr;

    printf("%s", user_agent_hdr);

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1)
    {
        clientlen = sizeof(client_addr);
        connfd = Accept(listenfd, (SA *)&client_addr, &clientlen);
        Getnameinfo((SA *)&client_addr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s %s).\n", hostname, port);
        pthread_t thread;
        Pthread_create(&thread, NULL, thread_main, &connfd);
    }

    return (0);
}

void process(int fd)
{
    int serverfd; // proxy >> server
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char http_hdr_to_server[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    int port = 0;
    rio_t rio_client, rio_server;

    Rio_readinitb(&rio_client, fd);
    Rio_readlineb(&rio_client, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET"))
    {
        printf("[%s] This method isn't implemented on Proxy server.\n", method);
        return ;
    }
    /* if hit cache */
    unsigned int hash_key = get_hash_key(uri);
    if (cache_table[hash_key].is_used)
    {
        char *cached_data_buf = cache_table[hash_key].data;
        Rio_writen(fd, cached_data_buf, strlen(cached_data_buf));
        return ;
    }

    parse_uri(uri, hostname, &port, path);
    set_http_request_hdr(http_hdr_to_server, hostname, &port, path, &rio_client);

    serverfd = connect_to_server(hostname, port);
    if (serverfd < 0)
    {
        printf("Connection failed");
        return ;
    }

    Rio_readinitb(&rio_server, serverfd);
    Rio_writen(serverfd, http_hdr_to_server, strlen(http_hdr_to_server));

    set_table_entry(hash_key);
    char *cache_buf = cache_table[hash_key].data;

    size_t len;
    while ((len = Rio_readlineb(&rio_server, buf, MAXLINE)))
    {
        printf("Proxy received %ld Bytes and send\n", len);
        Rio_writen(fd, buf, len);
        memcpy(cache_buf, buf, len);
        cache_buf += len;
    }
    Close(serverfd);
}

/* https://localhost:port <> ip:port */
void parse_uri(char *uri, char *hostname, int *port, char *path)
{
    char *port_ptr = '\0';
    char *path_ptr = '\0';
    char *hostname_ptr = strstr(uri, "//");
    /* https://localhost~ or localhost~ */
    hostname_ptr = hostname_ptr ? hostname_ptr+2 : uri;
    port_ptr = strchr(hostname_ptr, ':');
    if (port_ptr)
    {
        /* localhost:port */
        *port_ptr = '\0';
        sscanf(hostname_ptr, "%s", hostname);
        sscanf(port_ptr+1, "%d%s", port, path);
    }
    else
        /* localhost~ */
        sscanf(hostname_ptr, "%s", hostname);
}

void set_http_request_hdr(char *http_hdr, char *hostname, int *port, char *path, rio_t *rio_client)
{
    char buf[MAXLINE];
    char request_hdr[MAXLINE];
    char general_hdr[MAXLINE];
    char host_hdr[MAXLINE];

    sprintf(request_hdr, request_line_format, path);
    while (Rio_readlineb(rio_client, buf, MAXLINE))
    {
        if (!strcmp(buf, "\r\n"))
            break ;
        strcat(general_hdr, buf);
    }

    if (strlen(host_hdr) == 0)
        sprintf(host_hdr, host_hdr_format, hostname);

    sprintf(http_hdr, "%s%s%s%s", request_hdr, user_agent_hdr, general_hdr, "\r\n");
}

int connect_to_server(char *hostname, int port)
{
    char port_str[8];

    sprintf(port_str, "%s", port);
    return (Open_clientfd(hostname, port_str));
}

void *thread_main(void *targs)
{
    Pthread_detach(Pthread_self());
    process(*(int *)targs);
    Close(*(int *)targs);
    return (NULL);
}

unsigned int get_hash_key(char *str)
{
    unsigned long long hash = 5381; // odd, deficient N >> magic N
    char *ptr = str;
    while (*ptr)
    {
        hash = ((hash<<5) + hash) + *ptr;
        ptr++;
    }
    return ((unsigned int)(hash % MAX_HASH_TABLE_SIZE));
}

void set_table_entry(unsigned int hash_key)
{
    cache_table[hash_key].is_used = 1;
    memset(cache_table[hash_key].data, 0, MAX_OBJECT_SIZE);
}
