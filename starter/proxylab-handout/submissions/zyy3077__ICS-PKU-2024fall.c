/*
 * name: 赵媛媛
 * student-id: <2300013077>
 */
#include <stdio.h>
#include "csapp.h"
#include "cache.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
void *thread(void *vargp) ;
void handle_request(int connfd);

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);

    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    init_cache();
    listenfd = open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        pthread_create(&tid, NULL, thread, connfdp);
    }
}

void *thread(void *vargp) 
{
    // printf("thread created\n");
    int connfd = *((int *)vargp);
    pthread_detach(pthread_self());
    free(vargp);
    
    handle_request(connfd);
    close(connfd);
    return NULL;
}


void handle_request(int connfd)
{
    // ################# parse GET request line & check validation ###################
    // read request into buf
    rio_t rio; // connfd's buffer
    rio_readinitb(&rio, connfd);
    char buf[MAXLINE];
    int sta;
    if (sta = rio_readlineb(&rio, buf, MAXLINE) <= 0) { 
        if (sta < 0) // read request line error
            fprintf(stderr, "error: read request line error\n");
        close(connfd); // EOF
        return;
    }
    // parse buf into |GET|hostname,path|HTTP/1.0| and check
    // printf("input: %s", buf);

    char get_request[MAXLINE], url_request[MAXLINE], http_request[MAXLINE];
    if (sscanf(buf, "%s %s %s", get_request, url_request, http_request) != 3) { // parse request line error
        fprintf(stderr, "error: parsing %s failed\n", buf);
        close(connfd); 
        return;
    }
    if (strcmp(get_request, "GET")) { // invalid: "GET" required
        fprintf(stderr, "error: GET required\n");
        close(connfd); 
        return;
    }
    if (strncmp(url_request, "http://", strlen("http://"))) { // invalid: "http://" required
        fprintf(stderr, "error: http:// required\n");
        close(connfd); 
        return;
    }
    if (strcmp(http_request, "HTTP/1.0") && strcmp(http_request, "HTTP/1.1")) { // invalid: "HEEP/1.0" or "HTTP/1.1" required
        fprintf(stderr, "error: HTTP/1.0 or HTTP/1.1 required\n");
        close(connfd); 
        return;
    }
    if (exist_cache(url_request, connfd)) { // be stored in cache
        return;
    }
    
    char *hostname_ptr = url_request + strlen("http://"); // ignore the prefix, start from www.
    char *first_slash =  strchr(hostname_ptr, '/');
    if (first_slash == NULL) { // invalid: path required
        fprintf(stderr, "error: path required\n");
        close(connfd); 
        return;
    }
    // ------------------- if valid ----------------------
    
    char *port_pos = strchr(hostname_ptr, ':');
    char hostname[MAXLINE], path[MAXLINE], port[MAXLINE];
    // int hostname_len = first_slash - hostname_ptr;
    strcpy(path, first_slash);
    if (port_pos) { // specified connecting port
        *first_slash = '\0';
        strcpy(port, port_pos+1);
        *first_slash = '/';
        *port_pos = '\0';
        strcpy(hostname, hostname_ptr);
        *port_pos = ':';
    } else { // default port
        strcpy(port, "80");
        *first_slash = '\0';
        strcpy(hostname, hostname_ptr);
        *first_slash = '/';
    }
    // printf("host: %s\n", hostname);
    // printf("port: %s\n", port);
    // printf("path: %s\n", path);
    // ######################### parse request headers ########################
    char parsed_header[MAXLINE] = "";
    int specified_host = 0;
    while (1) {
        if (rio_readlineb(&rio, buf, MAXLINE) <= 0) break;
        // printf("input: %s", buf);
        if (!strcmp(buf, "\r\n")) break;
        if (!strncmp(buf, "Host:", strlen("Host:"))) {
            specified_host = 1;
        }
        // ignore 
        if (!strncmp(buf, "User-Agent:", strlen("User-Agent:")) ||
            !strncmp(buf, "Connection:", strlen("Connection:")) ||
            !strncmp(buf, "Proxy-Connection:", strlen("Proxy-Connection:")))
            continue;
        // concatenate
        strcat(parsed_header, buf);
    }
    if (specified_host == 0) {
        sprintf(buf, "Host: %s\r\n", hostname);
        strcat(parsed_header, buf);
    } 
    strcat(parsed_header, user_agent_hdr);
    strcat(parsed_header, "Connection: close\r\n");
    strcat(parsed_header, "Proxy-Connection: close\r\n");
    strcat(parsed_header, "\r\n");
    // ####################### connect to the host & send request ############################
    int clientfd;
    rio_t c_rio;
    clientfd = open_clientfd(hostname, port);
    if (clientfd < 0) { // error: fail to connect to host
        fprintf(stderr, "error: failed to connect to the host\n");
        close(connfd); 
        return;
    }
    rio_readinitb(&c_rio, clientfd);

    sprintf(buf, "GET %s HTTP/1.0\r\n%s", path, parsed_header);
    // printf("buf: %s", buf);
    rio_writen(clientfd, buf, strlen(buf)); // proxy to host
    int total_size = 0;
    int temp_size = 0;
    char data_buf[MAX_OBJECT_SIZE];
    while (temp_size = rio_readnb(&c_rio, buf, MAXLINE)) { // host to proxy
        total_size += temp_size;
        if (total_size <= MAX_OBJECT_SIZE) {
            memcpy(data_buf + total_size - temp_size, buf, temp_size); // restore data as a whole from host to data_buf
        }
        rio_writen(connfd, buf, temp_size); // proxy to client
    }
    if (total_size <= MAX_OBJECT_SIZE) {
        do_cache(url_request, data_buf, total_size);
    }
    close(clientfd);
    close(connfd);
    return;
}