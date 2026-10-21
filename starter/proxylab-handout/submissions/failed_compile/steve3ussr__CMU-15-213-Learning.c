#include <stdio.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"
#include <netdb.h>


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE  1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_ENTRY 8
#define uint            unsigned int
#define MAX_URL         1024
#define N_WORKER        32
#define N_FD_BUF        64

typedef struct{
    char msg[MAX_OBJECT_SIZE];
    int nleft;
    int index;
} proxy_msg_t;

sbuf_t fd_buf;
cache_t c;

/* You won't lose style points for including this long line in your code */
static char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void forward(int);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
int parse_req_line(char *req_line, char *method, char *url, char *version, int fd);
int parse_url(char *url, char *host, char *port, char *path, char *query, int fd);
uint is_prefix(char *prefix, char *string);
void check_header(char *entry, int *);
int append_msg (proxy_msg_t *msg_p, char *content);
void *worker(void *);


int parse_req_line(char *req_line, char *method, char *url, char *version, int fd){
    /*
     * this function will parse request line, detach method, url and version
     *
     * return value:
     *   - 0: no error
     *   - 1: error
     *
     * if error occurs, fd:
     *   - NULL: parse only
     *   - int : call clienterror, return HTTP error code
     */

    // TOO LONG request line
    if (strlen(req_line) >= 1024){
        if (fd) { clienterror(fd, 
                              req_line, 
                              "414", 
                              "URI Too Long", 
                              "The URI provided was too long for the proxy to process");}
        return 1;
    }

    // MALFORMED request line
    if (sscanf(req_line, "%s %s %s\r\n", method, url, version) != 3){
        if (fd) { clienterror(fd, 
                              method, 
                              "400", 
                              "Bad Request", 
                              "Proxy cannot process the client's request due to a client-side error");}
        return 1;}
    
    // MALFORMED method
    if (strcasecmp(method, "GET")){
        if (fd) { clienterror(fd, 
                              method, 
                              "501", 
                              "Not Implemented", 
                              "Proxy does not implement this method");}
        return 1;}

    // MALFORMED version
    if (strcasecmp(version, "HTTP/1.0") && strcasecmp(version, "HTTP/1.1")){
        if (fd) { clienterror(fd, 
                              version, 
                              "505", 
                              "Not Supported", 
                              "Proxy does not support this version");}
        return 1;}
    
    return 0;
}

int parse_url(char *url, char *host, char *port, char *path, char *query, int fd){
    /*
     * this function will detach components from url
     * if url is a static request, function will fill host, port, path
     * if url is a dynamic request, function will fill host, port, path and query
     *
     * return value:
     *   -  0: static
     *   -  1: dyna
     *   - -1: error
     *
     * and if error occurs, fd:
     *   - NULL: parse only
     *   - int : call clienterror, return HTTP error code
    */

    char tmp[MAXLINE];

    // MALFORMED scheme
    if (!sscanf(url, "http://%s", tmp)) {
        if (fd) { clienterror(fd, 
                              url, 
                              "422", 
                              "Unprocessable Content", 
                              "Proxy does not support this scheme");}
        return -1;}

    char *p_colon = strchr(tmp, ':');
    char *p_slash = strchr(tmp, '/');
    
    // MALFORMED path
    if ((!p_slash) || (p_colon > p_slash)) {  // no path || colon in path
        if (fd) { clienterror(fd, 
                              url, 
                              "422", 
                              "Unprocessable Content", 
                              "Proxy cannot parse path");}
        return -1;}

    // detach host, port, path_query(tmp)
    *p_slash = '\0';
    if (p_colon){
        *p_colon = '\0';
        strcpy(host, tmp);
        strcpy(tmp, p_colon+1);     // tmp: 15213/cgi-bin/adder?1&2

        strcpy(port, tmp);
        strcpy(tmp, p_slash+1);}    // tmp: cgi-bin/adder?1&2

    else{
        strcpy(host, tmp);
        strcpy(port, "80");
        strcpy(tmp, p_slash+1);}    // tmp: cgi-bin/adder?1&2

    // MALFORMED port
    for (char *p=port; *p!='\0'; p++){
        if (!isdigit(*p)) {
            if (fd) { clienterror(fd, 
                                  url, 
                                  "422", 
                                  "Unprocessable Content", 
                                  "Malformed port");}
            return -1;}}
    int port_int = atoi(port);
    if (port_int > 65535){
        if (fd) { clienterror(fd, 
                              url, 
                              "422", 
                              "Unprocessable Content", 
                              "Malformed port");}
        return -1;}

    // MALFORMED host
    if (strchr(host, '?')){
        if (fd) { clienterror(fd, 
                              url, 
                              "422", 
                              "Unprocessable Content", 
                              "Malformed host");}
        return -1;}

    // detach path, query from path_query(tmp)
    char *p_quest = strchr(tmp, '?');
    int is_dyna = 0;
    strcpy(path, "/"); strcpy(query, "");

    if (p_quest) { *p_quest = '\0'; strcpy(query, p_quest+1); is_dyna=1;}
    strcat(path, tmp);
    if (path[strlen(path)-1] == '/') {strcat(path, "home.html");}

    // UNSAFE path
    if (strstr(path, "..")) {
        if (fd) { clienterror(fd, 
                              url, 
                              "422", 
                              "Unprocessable Content", 
                              "malicious cotnent huh?");}
        return -1;
    }


    // debug print
    // printf("\t[Request Line: host   \t] %s\n", host);
    // printf("\t[Request Line: port   \t] %s\n",port);
    // printf("\t[Request Line: path   \t] %s\n", path);
    // printf("\t[Request Line: query  \t] %s\n", query);

    return is_dyna;
}

uint is_prefix(char *prefix, char *string){
    return strncmp(prefix, string, strlen(prefix))==0;
}

void check_header(char *entry, int *fp) {
    /*
     * description:
     * if entry is "user-agent"/"connection"/"proxy-connection": truncate
     * else if entry is "host": record, change flag
     * else: do nothing
     */

    if (is_prefix("Host: ", entry))                     { *fp = *fp | 1; }
    else if (is_prefix("User-Agent: ", entry))          { *entry='\0'; }
    else if (is_prefix("Connection: ", entry))          { *entry='\0'; }
    else if (is_prefix("Proxy-Connection: ", entry))    { *entry='\0'; }
    else                                                      { return; }
}

int append_msg (proxy_msg_t *msg_p, char *content){
    const int n = (int)strlen(content);
    if (n+msg_p->index > MAX_OBJECT_SIZE) {return 1;}

    memcpy(msg_p->msg+msg_p->index, content, n);
    msg_p->index += n;
    return 0;
}

void forward(int client_fd)
{
    // Step 1: initial rio-clientfd
    rio_t rio_client; Rio_readinitb(&rio_client, client_fd);
    char buf[MAXLINE];


    // Step 2: READ request line
    if (!Rio_readlineb(&rio_client, buf, MAXLINE)) return;


    // Step 3: PARSE request line
    char method[MAX_URL], url[MAX_URL], version[MAX_URL], host[MAX_URL], port[MAX_URL], path[MAX_URL], query[MAX_URL];
    if (parse_req_line(buf, method, url, version, client_fd)) {return;}


    // Step 4: PARSE url, check if is dyna
    int is_dyna; 
    if ((is_dyna=parse_url(url, host, port, path, query, client_fd)) == -1) {return;}


    // Step 5: check if hit cache
    cache_acquire_lock(&c);
    cache_show(&c);
    int res_search = cache_search(&c, url);

    //// Step 5.1: cache hit! return directly
    if (res_search >= 0){
        entry_t *ep = c.entries[res_search];
        Rio_writen(client_fd, ep->value, ep->value_length);
        cache_release_lock(&c);
        return;}

    //// Step 5.2: cache miss, evict old entry
    if (res_search == -2) cache_evict(&c, url);

    //// Step 5.3: cache miss, slot is empty
    cache_release_lock(&c);


    // Step 6: MAKE Proxy-forward (Step 5.2 and 5.3 all required proxy to forward request to server)

    //// Step 6.0: initial proxy_msg_t object
    proxy_msg_t proxy_forw;
    proxy_forw.index = 0;

    //// Step 6.1: fill request line
    if (is_dyna)    sprintf(buf, "GET %s?%s HTTP/1.0\r\n", path, query);
    else            sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    if (append_msg(&proxy_forw, buf)) return;

    //// Step 6.2: read headers, check and append to proxy-forward
    int flag_host_hdr = 0;
    while (1) {
        Rio_readlineb(&rio_client, buf, MAXLINE);
        if (!strcmp(buf, "\r\n")) break;
        check_header(buf, &flag_host_hdr);
        if (append_msg(&proxy_forw, buf)) return;}

    //// Step 6.3: append host
    if (!flag_host_hdr){
        sprintf(buf, "Host: %s:%s\r\n", host, port);
        if (append_msg(&proxy_forw, buf)) return;
    }

    //// Step 6.4: append user-agent, connection, proxy-connection
    if (append_msg(&proxy_forw, user_agent_hdr)) return;
    if (append_msg(&proxy_forw, "Connection: Close\r\n")) return;
    if (append_msg(&proxy_forw, "Proxy-Connection: Close\r\n")) return;
    if (append_msg(&proxy_forw, "\r\n")) return;    // ending


    // Step 7: connect to real server
    int server_fd = Open_clientfd(host, port); 
    rio_t rio_server; Rio_readinitb(&rio_server, server_fd);
    printf("Connected to server (%s:%s)\n", host, port);


    // Step 8: SEND Proxy-forw
    Rio_writen(server_fd, proxy_forw.msg, strlen(proxy_forw.msg));
    strcpy(proxy_forw.msg, "");
    proxy_forw.index = 0;


    // Step 9: GET Proxy-reply headers
    //// Step 9.0: initial proxy-reply
    proxy_msg_t proxy_reply;
    proxy_reply.index = 0;

    //// Step 9.1: get headers, get content-length
    int content_length = 0;
    while (Rio_readlineb(&rio_server, buf, MAXLINE)){
        if (is_prefix("Content-length: ", buf)) content_length = atoi(buf+16);
        append_msg(&proxy_reply, buf);
        if (!strcmp(buf, "\r\n")) break; }  // check if is \r\n
    strcpy(buf, "");

    //// Step 9.2: GET Proxy-reply entity
    while (content_length) {
        const int tmp = (int)Rio_readnb(&rio_server, buf, 8000);
        memcpy(proxy_reply.msg+proxy_reply.index, buf, tmp);
        proxy_reply.index += tmp;
        content_length -= tmp;}

    //// Step 9.3: if can be add to cache, do it
    if ((proxy_reply.index <= MAX_OBJECT_SIZE) && (c.curr_cache_size + proxy_reply.index <= c.max_cache_size)){
        cache_acquire_lock(&c);
        cache_add(&c, url, proxy_reply.msg, proxy_reply.index);
        cache_release_lock(&c);}

    //// Step 9.4: send proxy-reply
    Rio_writen(client_fd, proxy_reply.msg, proxy_reply.index);

    
    // Step 10: Teardown
    Close(server_fd);
}

void *worker(void *argvp){
    Pthread_detach(pthread_self());
    while (1) {
        const int connfd = sbuf_remove(&fd_buf);
        printf("worker(%lu) get connfd(%d) from buffer\n", (unsigned long)pthread_self(), connfd);
        forward(connfd);
        Close(connfd);}
}

int main(const int argc, char **argv)
{
    // Step 1: check argument 
    if (argc != 2){
        fprintf(stderr, "Proxy usage: %s <proxy_port_number>\n", argv[0]);
        exit(1);}

    // Step 2: listen
    int listenfd = Open_listenfd(argv[1]);

    // Step 3: init fd_buf
    sbuf_init(&fd_buf, N_FD_BUF);

    // Step 4: pre-threading
    pthread_t tid;
    for(int i=0; i<N_WORKER; i++){
        Pthread_create(&tid, NULL, worker, NULL);}
    printf("%d worker threads created\n", N_WORKER);

    // Step 5: initial cache
    cache_init(&c, MAX_CACHE_ENTRY, MAX_CACHE_SIZE, MAX_OBJECT_SIZE);

    // Step 6: accept and forward, let workers do everything
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        const int connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        sbuf_insert(&fd_buf, connfd);}

    return 0;
}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE];

    /* Print the HTTP response headers */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n\r\n");
    Rio_writen(fd, buf, strlen(buf));

    /* Print the HTTP response body */
    sprintf(buf, "<html><title>Tiny Error</title>");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<body bgcolor=""ffffff"">\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
    Rio_writen(fd, buf, strlen(buf));
}
