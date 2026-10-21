#include <stdio.h>
#include <csapp.h>
#include "sbuf.h"
#include "map.h"

/* Recommended max cache and object sizes */

#define MAX_POOL 16
// #define LOG_FILE "./proxy_log.txt"

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

static sbuf_t pool;
static Map map;
static sem_t map_mutex;

#ifdef LOG_FILE
static char logger[MAXBUF];
static int log_fd;
#endif

void* thread_routine(void* args);
void serve_client(int client_fd, void* client_buff, void* server_buff, pthread_t tid);
void parse_url(char* url, char* host, char* port, char* uri);

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stdout, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    
    // init thread pool
    subf_init(&pool, MAX_POOL);
    int i;
    pthread_t tid;
    for (i = 0; i < MAX_POOL; i++) Pthread_create(&tid, NULL, thread_routine, NULL);

    // init cache(just a map)
    map_init(&map);
    Sem_init(&map_mutex, 0, 1);
    
    int listen_fd = Open_listenfd(argv[1]);
    struct sockaddr_storage client_add;
    socklen_t add_len;
    char host[MAXBUF];
    char port[MAXBUF];

#ifdef LOG_FILE
    // init logger    
    log_fd = Open(LOG_FILE, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
    time_t current_time;
    time(&current_time);
    sprintf(logger, "proxy is ready, current time is: %s\n", ctime(&current_time));
    Write(log_fd, logger, strlen(logger));
#endif   
    
    while (1) {
        add_len = sizeof(client_add);
        int client_fd = Accept(listen_fd, (SA*)&client_add, &add_len);
        Getnameinfo((SA*)&client_add, add_len, host, MAXBUF, port, MAXBUF, 0);
#ifdef LOG_FILE
        sprintf(logger, "connect:to:client:%s:port:%s\n", host, port);
        Write(log_fd, logger, strlen(logger));
#endif
        sbuf_offer_last(&pool, client_fd);
    }
#ifdef LOG_FILE
    Close(log_fd);
#endif
    Close(listen_fd);
    sbuf_deinit(&pool);
    return 0;
}

void* thread_routine(void* args) {
    pthread_t tid = Pthread_self();
    Pthread_detach(tid);
    void* client_buff = Malloc(MAXBUF * sizeof(unsigned char));
    void* server_buff = Malloc(MAXBUF * sizeof(unsigned char));
    while (1) {
        int client_fd = sbuf_poll_first(&pool);
        serve_client(client_fd, client_buff, server_buff, tid);
        Close(client_fd);
    }
    Free(client_buff);
    Free(server_buff);
}

void serve_client(int client_fd, void* client_buff, void* server_buff, pthread_t tid) {
#ifdef LOG_FILE
    char thread_local[MAXBUF];
#endif

    rio_t read_client;
    Rio_readinitb(&read_client, client_fd);
    Rio_readlineb(&read_client, client_buff, MAXBUF);
    char method[MAXBUF], url[MAXBUF], version[MAXBUF];
    sscanf(client_buff, "%s %s %s", method, url, version);
    if (!strcmp(method, "GET")) {
        // parse url into: host, port, uri
        char host[MAXBUF], port[MAXBUF], uri[MAXBUF];
        parse_url(url, host, port, uri);

        // query buffer
        P(&map_mutex);
        char* buff = map_get(&map, uri);
        V(&map_mutex);

        if (buff != NULL) {
            Rio_writen(client_fd, buff, strlen(buff));
            return;
        }
        
        // consturct HTTP request
        // request line
        sprintf(server_buff, "%s %s HTTP/1.0\r\n", method, uri);

        // request header
        sprintf(server_buff, "%sHost: %s\r\n", (char*)server_buff, host);
        sprintf(server_buff, "%s%s%s%s", (char*)server_buff, user_agent_hdr, connection_hdr, proxy_connection_hdr);
        // read headers from client
        Rio_readlineb(&read_client, client_buff, MAXBUF);
        while (strcmp(client_buff, "\r\n")) {
            char* has_connection_hdr = strstr(client_buff, "Connection:");
            char* has_proxy_connection_hdr = strstr(client_buff, "Proxy-Connection:");
            char* has_host_hdr = strstr(client_buff, "Host:");
            if (has_connection_hdr == NULL && has_proxy_connection_hdr == NULL && has_host_hdr == NULL) sprintf(server_buff, "%s%s", (char*)server_buff, (char*)client_buff);
            Rio_readlineb(&read_client, client_buff, MAXBUF);
        }

        // HTTP empty line
        sprintf(server_buff, "%s\r\n", (char*)server_buff);

        int request_size = strlen((char*)server_buff);

#ifdef LOG_FILE
        sprintf(thread_local, "thread:%lu:proxy:request:\n", tid);
        int request_prologue = strlen(thread_local);
        sprintf(thread_local, "%s%s", thread_local, (char*)server_buff);
        Write(log_fd, thread_local, request_prologue + request_size); 
#endif
        
        // write to server
        int server_fd = Open_clientfd(host, port);
        Rio_writen(server_fd, server_buff, request_size);

        // read HTTP response from server
        rio_t read_server;
        rio_readinitb(&read_server, server_fd);

        ssize_t n;
        int real_size = 0;
        char tmp[MAX_CACHE_SIZE];
        while ((n = Rio_readnb(&read_server, client_buff, MAXBUF))) {
            memcpy(tmp + real_size, client_buff, n);
            real_size += n;
        }
        Rio_writen(client_fd, tmp, real_size);

        P(&map_mutex);
        map_put(&map, uri, tmp);
        V(&map_mutex);

#ifdef LOG_FILE
        sprintf(thread_local, "thread:%lu:proxy:response\n", tid);
        Write(log_fd, thread_local, strlen(thread_local));
#endif
    }
}

void parse_url(char* url, char* host, char* port, char* uri) {
    char* has_protocol = strstr(url, "//");
    // url with `http(s)://` need to be trimed
    if (has_protocol != NULL) url = has_protocol + 2;
    char* has_port = strstr(url, ":");
    // url with port
    if (has_port != NULL) {
        *has_port = '\0';
        strcpy(host, url);
        url = has_port + 1;
        char* split = strstr(url, "/");
        if (split == NULL) {
            strcpy(port, url);
            sprintf(uri, "%s", "/");
        } else {
            strcpy(uri, split);
            *split = '\0';
            strcpy(port, url);
        }
    } else {
        sprintf(port, "%d", 80);
        char* split = strstr(url, "/");
        if (split == NULL) {
            strcpy(host, url);
            sprintf(uri, "%s", "/");
        }
        else {
            strcpy(uri, split);
            *split = '\0';
            strcpy(host, url);
        }
    }
}