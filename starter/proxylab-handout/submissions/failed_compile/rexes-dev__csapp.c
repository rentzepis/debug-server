#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CacheBlock {
    char *url;
    int *exist;
    int *lru;
    int *size;
    char *obj;

} CacheBlock;

CacheBlock* cache;

int readcnt = 0;
sem_t mutex, w;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void* handle_conn(void* vargp);
int reader(int connfd, char* url);
void writer(char *url, char* res, int sz);

int main(int argc, char **argv)
{
    // Usage: argv[0] <port>
    if (argc != 2) {
        fprintf(stderr, "usage %s <port>\n", argv[0]);
        exit(1);
    }

    // Initialize cache
    cache = malloc(10*sizeof(CacheBlock));
    for (int i = 0; i < 10; ++i){
        cache[i].url = calloc(1024, sizeof(char));
        cache[i].exist = calloc(1, sizeof(int));
        cache[i].lru = calloc(1, sizeof(int));
        cache[i].size = calloc(1, sizeof(int));
        cache[i].obj = calloc(MAX_OBJECT_SIZE, sizeof(char));
    }
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);

    pthread_t tid;
    int listenfd, connfd;
    // char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    
    listenfd = Open_listenfd(argv[1]);
    printf("Proxy has started listening connections on port %s\n", argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        // Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        // printf("Proxy has accepted a connection from (%s, %s)\n", hostname, port);
        // handle_conn(connfd);
        Pthread_create(&tid, NULL, handle_conn, (void *) connfd);
              
    }

    // printf("%s", user_agent_hdr);
    return 0;
}

void *handle_conn(void* vargp){
    int connfd = (int) vargp;
    Pthread_detach(pthread_self());
    rio_t rio;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE], pathname[MAXLINE];
    Rio_readinitb(&rio, connfd);
    
    /* Request line (method, uri, version) */ 
    if (!Rio_readlineb(&rio, buf, MAXLINE)) return;
    sscanf(buf, "%s %s %s", method, uri, version);
    if (strcasecmp(method, "GET")) {

       //clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method"); 
        char errormsg[MAXLINE];

        sprintf(errormsg, "HTTP/1.0 %s %s\r\n", "501", "Not Implemented");
        Rio_writen(connfd, errormsg, strlen(errormsg));
        sprintf(errormsg, "Content-type: text/html\r\n\r\n");
        Rio_writen(connfd, errormsg, strlen(errormsg));

        sprintf(errormsg, "<html><title>Proxy error</title>");
        Rio_writen(connfd, errormsg, strlen(errormsg));
        sprintf(errormsg, "<body bgcolor=""ffffff"">\r\n");
        Rio_writen(connfd, errormsg, strlen(errormsg));
        sprintf(errormsg, "%s: %s\r\n", "501", "Not Implemented");
        Rio_writen(connfd, errormsg, strlen(errormsg));
        sprintf(buf, "<p>%s: %s\r\n", "Proxy does not implement this method", method);
        Rio_writen(connfd, buf, strlen(buf));
        sprintf(buf, "<hr><em>Proxy</em>\r\n");
        Rio_writen(connfd, buf, strlen(buf));
        return NULL;
    }
    // read_requesthdrs(&rio); // Ignore request headers
    char http[] = "http://";
    char www[] = "www.";
    char port[6];
    strcpy(port, "80");
    char *p;
    char *pch;
    // http://www.cmu.edu:8080/hub/index.html
    // port = 8080;
    // hostname = cmu.com 
    // pathname = /hub/index.html
    p = uri;
    pch = strstr(p, http);
    if (pch) {
        // Contains http://
        p += 7;
        // www.cmu.edu:8080/hub/index.html
    }
    pch = strstr(p, www);
    if (pch) {
        // Contains www.
        p += 4;
        // cmu.edu:8080/hub/index.html
    }

    pch = strstr(p, "/");
    if (pch == NULL) {
        // cmu.edu:8080
        strcpy(pathname, "/");
        pch = strstr(p, ":");
        if (pch){
            strcpy(port, pch+1);
            *pch = 0;
            strcpy(hostname, p);
            *pch = ':';
        }
    }
    else {
        // cmu.edu:8080/hub/index.html
        strcpy(pathname, pch);
        // pathname = /hub/index.html
        *pch = 0;
        // printf("%s\n", p);
        // p --> example.com\0
        char *pp = strstr(p, ":");
        if (pp){
            strcpy(port, pp+1);
            *pp = 0;
            strcpy(hostname, p);
            *pp = ':';
        }
        else {
            strcpy(hostname, p);
        }
        pch = '/';
    }
    // printf("Hostname: %s\n", hostname);
    // printf("Pathname: %s\n", pathname);
    // printf("Port: %s\n", port);
    char search[MAXLINE];
    strcpy(search, hostname);
    sprintf(search, "%s:%s%s", search, port, pathname);
    if (reader(connfd, search)) {
        Close(connfd);
        return NULL;
    }

    char req[MAXLINE];
    char reqline[MAXLINE];
    strcpy(req, "GET");
    // // GET /index.html HTTP/1.0
    sprintf(req, "%s %s HTTP/1.0\r\n", req, pathname);
    sprintf(req, "%sHost: www.%s\r\n", req, hostname); // Host: www.example.com
    sprintf(req, "%s%s", req, user_agent_hdr);
    sprintf(req, "%sConnection: close\r\nProxy-Connection: close\r\n", req);
    Rio_readlineb(&rio, reqline, MAXLINE);
    // printf(req);
    while (strcmp(reqline, "\r\n")){
        // printf("%s", reqline);
        if (strncmp(reqline, "Host", 4) && strncmp(reqline, "User-Agent", 10) && strncmp(reqline, "Connection", 10) && strncmp(reqline, "Proxy-Connection", 16)){
            sprintf(req, "%s%s", req, reqline);
        }
        Rio_readlineb(&rio, reqline, MAXLINE);
    }
    sprintf(req, "%s\r\n", req);
    // printf(req);
    /*
        response line
        response headers

        response body
    */
    int sz = 0;
    int clientfd;
    rio_t crio;
    clientfd = Open_clientfd(hostname, port);
    Rio_readinitb(&crio, clientfd);
    Rio_writen(clientfd, req, strlen(req));
    char response[MAXLINE];
    char resline[MAXLINE];
    Rio_readlineb(&crio, resline, MAXLINE);
    strcpy(response, resline);
    while (strcmp(resline, "\r\n")){
        Rio_readlineb(&crio, resline, MAXLINE);
        sprintf(response, "%s%s", response, resline);
    }
    Rio_writen(connfd, response, strlen(response));
    sz += strlen(response);
    char savebuf[MAX_OBJECT_SIZE];
    strcpy(savebuf, response);
    int cnt;
    while(cnt = Rio_readnb(&crio, resline, MAXLINE)){
        Rio_writen(connfd, resline, cnt);
        if (sz <= MAX_OBJECT_SIZE){
            memcpy(savebuf+sz, resline, cnt);
        }
        sz += cnt;
    }
    if (sz <= MAX_OBJECT_SIZE){
        writer(search, savebuf, sz);
    }
    Close(clientfd);
    Close(connfd);
    return NULL;
}

int reader(int connfd, char *url){
    int exist = 0;
    P(&mutex);
    readcnt++;
    if (readcnt == 1) P(&w);
    V(&mutex);

    for (int i = 0; i < 10; ++i){
        if (cache[i].exist && !strcmp(cache[i].url, url)){
            Rio_writen(connfd, cache[i].obj, *cache[i].size);
            exist = 1;
            *cache[i].lru = 0;
            break;
        }
    }
    if (exist) {
        for (int i = 0; i < 10; ++i){
            *cache[i].lru += 1;
        }
    }
    P(&mutex);
    readcnt--;
    if (readcnt == 0) V(&w);
    V(&mutex);
    return exist;
}

void writer(char *url, char *res, int sz) {
    P(&w);
    int found = 0;
    int max_lru = 0;
    int max_lru_ind = 0;
    for (int i = 0; i < 10; ++i){
        if (!cache[i].exist){
            cache[i].exist = 1;
            *cache[i].lru = 0;
            memcpy(cache[i].obj, res, sz);
            strcpy(cache[i].url, url);
            *cache[i].size = sz;
            found = 1;
            break;
        }
        if (max_lru < *cache[i].lru) {
            max_lru = *cache[i].lru;
            max_lru_ind = i;
        }
    }

    if (!found){
        *cache[max_lru_ind].lru = 0;
        memcpy(cache[max_lru_ind].obj, res, sz);
        strcpy(cache[max_lru_ind].url, url);
        *cache[max_lru_ind].size = sz;
    }

    for (int i = 0; i < 10; ++i){
        *cache[i].lru += 1;
    }

    V(&w);
}