#include "csapp.h"
#include <stdio.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
/* define my max cache objects number */
#define MAX_OBJECT_NUMBER 8

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_hdr = "Proxy-Connection: close\r\n";

struct UriInfo {
    char hostname[MAXLINE];
    char port[MAXLINE];
    char path[MAXLINE];
};

struct Cache
{
    char uri[MAXLINE];
    char content[MAX_OBJECT_SIZE];
    int is_used;
    int cnt;
};

/* Global variables */
static struct Cache cache[MAX_OBJECT_NUMBER];
static sem_t reader_mutex, writer_mutex;
static int reader_cnt = 0;

void doit(int fd);
void parse_uri(char *uri, struct UriInfo *uriinfo);
void encapsulate_request(rio_t *client, char *request, struct UriInfo *uriinfo);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void *thread(void *varge);
void mutex_init();
void cache_init();
int read_cache(int fd, char *uri);
void write_cache(char *buf, char *uri);

int main(int argc, char **argv)  {
    int listenfd, *connfd_ptr;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(1);
    }

    /* initialize */
    mutex_init();
    cache_init();

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	    clientlen = sizeof(clientaddr);
        connfd_ptr = Malloc(sizeof(int));
	    *connfd_ptr = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, 
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfd_ptr);
    }
    return 0;
}

void *thread(void *varge) {
    int connfd = *((int *) varge);
    Pthread_detach(pthread_self());
    Free(varge);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void mutex_init() {
    Sem_init(&reader_mutex, 0, 1);
    Sem_init(&writer_mutex, 0, 1);
    return;
}

void cache_init() {
    for (int i = 0; i < MAX_OBJECT_NUMBER; i++) {
        cache[i].cnt = 0;
        cache[i].is_used = 0;
    }
    return;
}

int read_cache(int fd, char *uri) {
    // if object read from cache, return 1
    int read_from_cache = 0;

    P(&reader_mutex);
    reader_cnt++;
    if (reader_cnt == 1) {  /* First in */
        P(&writer_mutex);
    }
    V(&reader_mutex);

    /* Critical section */
    /* Reading happens */

    for (int i = 0; i < MAX_OBJECT_NUMBER; i++) {
        if (cache[i].is_used && (strcmp(uri, cache[i].uri) == 0)) {
            read_from_cache = 1;
            // return cache object
            Rio_writen(fd, cache[i].content, MAX_OBJECT_SIZE);
            // recently used counter
            cache[i].cnt++;
            break;
        }
    }

    /* Critical section */
    P(&reader_mutex);
    reader_cnt--;
    if (reader_cnt == 0) {  /* Last out */
        V(&writer_mutex);
    }
    V(&reader_mutex);

    return read_from_cache;
}

void write_cache(char *buf, char *uri) {
    int min_cnt = cache[0].cnt;
    int eviction_position = 0;

    P(&writer_mutex);

    /* Critical section */
    /* Writing happens */

    for (int i = 0; i < MAX_OBJECT_NUMBER; i++) {
        if (cache[i].is_used == 0) {    // find an empty cache
            eviction_position = i;
            break;
        }
        if (cache[i].cnt < min_cnt) {   // evict the least used objects
            eviction_position = i;
            min_cnt = cache[i].cnt;
        }
    }
    strcpy(cache[eviction_position].uri, uri);
    strcpy(cache[eviction_position].content, buf);
    cache[eviction_position].cnt = 0;
    cache[eviction_position].is_used = 1;

    /* Critical section */
    V(&writer_mutex);
    return;
}

void doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    rio_t rio_client, rio_server;
    struct UriInfo u;

    /* Read request line and headers */
    Rio_readinitb(&rio_client, fd);
    if (!Rio_readlineb(&rio_client, buf, MAXLINE))
        return;
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    // only suopport GET method
    if (strcasecmp(method, "GET")) {
        clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
        return;
    }

    // read from cache
    char uri_temp[MAXLINE];
    strcpy(uri_temp, uri);
    if (read_cache(fd, uri_temp)) {
        return;
    }

    // parse URI from GET request
    parse_uri(uri, &u);
    // get server fd
    int server_fd = Open_clientfd(u.hostname, u.port);
    Rio_readinitb(&rio_server, server_fd);

    char request[MAXLINE];
    // initial requset line, and use path info
    sprintf(request, "GET %s HTTP/1.0\r\n", u.path);
    // encapsulate client request
    encapsulate_request(&rio_client, request, &u);
    // sent encapsulated request to the server
    Rio_writen(server_fd, request, strlen(request));

    int len = 0;
    int object_len = 0;
    char object_buf[MAX_OBJECT_SIZE];
    // get response from server and sent to the client
    while ((len = Rio_readlineb(&rio_server, buf, MAXLINE)) > 0) {
        Rio_writen(fd, buf, len);

        // calculate object buf size
        if (object_len + len < MAX_OBJECT_SIZE) {
            strcpy(object_buf + object_len, buf);
            object_len += len;
        }
    }

    // write to cache
    if (object_len < MAX_OBJECT_SIZE) {
        write_cache(object_buf, uri_temp);
    }

    Close(server_fd);
    return;
}

void parse_uri(char *uri, struct UriInfo *uriinfo) {
    char *ptr_hostname = strstr(uri, "//");
    if (ptr_hostname) {     // find host name
        ptr_hostname = ptr_hostname + 2;
    }
    else {
        ptr_hostname = uri;
    }

    char *ptr_port = strstr(ptr_hostname, ":");     // find port numebr
    if (ptr_port) {
        int port;   // for sscanf port number
        sscanf(ptr_port + 1, "%d%s", &port, uriinfo->path);
        sprintf(uriinfo->port, "%d", port);     // store it into uriinfo
        *ptr_port = '\0';
    }
    else {      // port not exist
        char *ptr_path = strstr(ptr_hostname, "/");
        if (ptr_path) {         // find path
            strcpy(uriinfo->path, ptr_path);
            strcpy(uriinfo->port, "80");    // defualt port numebr
            *ptr_path = '\0';
        }
    }

    strcpy(uriinfo->hostname, ptr_hostname);    // get final host name
    return;
}

void encapsulate_request(rio_t *rio, char *request, struct UriInfo *uriinfo) {
    char buf[MAXLINE];

    while(Rio_readlineb(rio, buf, MAXLINE) > 0) {   // read from client
        // ignore these infos
        if (strstr(buf, "\r\n")) break;

        if (strstr(buf, "Host:")) continue;
        if (strstr(buf, "User-Agent:")) continue;
        if (strstr(buf, "Connection:")) continue;
        if (strstr(buf, "Proxy Connection:")) continue;
        
        // add infos into initial request line
        sprintf(request, "%s%s", request, buf);
    }

    // add the other request headers
    sprintf(request, "%sHost: %s:%s\r\n", request, uriinfo->hostname, uriinfo->port);
    sprintf(request, "%s%s%s%s\r\n", request, user_agent_hdr, conn_hdr, proxy_hdr);
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXLINE];

    // build the HTTP response body
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

    // print the HTTP response
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));

    // print the HTTP response body
    Rio_writen(fd, body, strlen(body));
}