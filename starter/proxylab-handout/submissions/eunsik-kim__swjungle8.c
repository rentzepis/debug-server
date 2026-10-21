/*  
 * It is simple proxy. it is followed by CNU Assign(http://csapp.cs.cmu.edu/3e/proxylab.pdf)
 * 1. foward-proxy server 
 * 2. concurrent processing as prethreded server from textbook
 * 3. caching response from server by LRU policy. Using linked list to allocate cache.
 *      When cache miss, checked duplicate cache and if it overlaps, refer same data simulataneously.
 *      Also synchronized thread with Reader-writer strategy in textbook
 * Additionaly, some exception handling was done to prevent the server from going down. 
 */
/* $begin proxy.c */
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

/* basic proxy written in textbook */
int request_transfer(int clientfd, rio_t *rio);
int parse_header(int clientfd, char *hostname, char *method, char *uri);
void response_transfer(int clientfd, int serverfd, rio_t *rio);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void alarm_handler(int sig);

/* prethreading server in textbook */
void *thread(void *vargp);

/* linked list for saving cache data */ 
typedef struct node_l node_l;
struct node_l {
    node_l *left;
    node_l *right;
    int responseHeaderSize;
    int responseBodySize;
    int requestHeaderSize;
    char *requestHeader;
    char *responseHeader;
    char *responseBody;
};
typedef struct {
    node_l *head;
    node_l *tail;
    int size;
    int cnt;
} linked_list;
static linked_list *LList;  

void free_LList();
void insert_into_tail(node_l *newNode);
void pop_node(node_l *startNode);
void free_node(node_l *deleteNode);

/* manipulate refer_cnt */
typedef enum {PLUS, MINUS} dir;
void up_down_refercnt(node_l *deleteNode, dir direction);

/* shared variables for reader-writer problem(reader first) */
int readcnt = 0;
sem_t mutex, w;
#define READLOCK() \
    do { \
        P(&mutex);\
        readcnt++;\
        if (readcnt == 1){\
            P(&w);}\
        V(&mutex);\
    } while (0)
#define READUNLOCK() \
    do { \
        P(&mutex); \
        readcnt--; \
        if (readcnt == 0){\
            V(&w); }\
        V(&mutex); \
    } while (0)
#define MAX_REFERENCE 10

int main(int argc, char **argv) 
{
    int listenfd, *clientfd;
    struct sockaddr_storage clientaddr;
    char clienthost[MAXLINE], clientport[MAXLINE];
    socklen_t clientlen;
    pthread_t tid;

    LList = (linked_list *)malloc(sizeof(linked_list));
    LList->head = NULL;
    LList->tail = NULL;
    LList->size = 0;
    LList->cnt = 0;
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);

    signal(SIGPIPE, SIG_IGN);       // if write for disconnected fd, ignore terminating thread 
    signal(SIGINT, free_LList);     // if intrrupt, free linked list for avoiding memory leak
    /* checking proxy port args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }
    listenfd = Open_listenfd(argv[1]);
    int i = 0;
    while (1) {
        clientlen = sizeof(clientaddr);
        clientfd = (int *)Malloc(sizeof(int));
        *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, clienthost, MAXLINE, clientport, MAXLINE, 0);
        printf("Accepted connection from client (%s, %s)\n", clienthost, clientport);
        Pthread_create(&tid, NULL, thread, clientfd);
        // Pthread_join(tid, NULL);     // if not detach, use join for memory leak
        printf("It's my %d circle.\n", ++i);
        printf("\n");
    }
    return 0;
}

void *thread(void *vargp)
{    
    Pthread_detach(pthread_self());
    printf("Hi, I'm %ld tid.\n", pthread_self());
    int serverfd, clientfd = *((int *)vargp);
    Free(vargp);
    rio_t rio;

    Rio_readinitb(&rio, clientfd);
    if ((serverfd = request_transfer(clientfd, &rio)) != -1)
        response_transfer(clientfd, serverfd, &rio);
    
    printf("====This connection is terminated====\n");
    Close(clientfd);
    return NULL;
}

/* 
 * filling missing header(host, user-agent, proxy-connection, connection cause of instruction pdf 4.2) 
 * and transfer request given from client to server, origin destination 
 */
int request_transfer(int clientfd, rio_t *rio)
{   
    char buf[MAXLINE], hostname[MAXLINE], con[40], proxcon[40];
    char method[10], uri[MAXLINE], version[10];
    int serverfd, isUserAgent=0, isHost=0, isCon=0, isProxCon=0;
    
    Rio_readlineb(rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
    printf("Request uri: %s\n", uri);

    // ignore favicon request
    if (strstr(uri, "favicon")) {
        printf("Couldn't find the favicon.\n");
        return -1;
    }
    // only GET request
    if (strcasecmp(method, "GET")) { 
        clienterror(clientfd, method, "501", "Not implemented", "Server does not implemented this method");
        return -1;
    }
    // only prefix string 'http://'
    if (!strstr(uri, "http://")) { 
        clienterror(clientfd, "URI", "501", "Not implemented", "Server does not implemented access without 'http://' ");
        return -1;
    }

    // exception for chrome access 
    char *headerend;
    if ((headerend = strstr(rio->rio_buf, "\r\n\r\n")) != NULL)
        *(headerend + 4) = '\0';

    // reader sync
    READLOCK();
    // cached data check
    node_l *startNode = LList->head;
    while (startNode != NULL) {
        // compare header size and momeory of request with cache data
        if ((startNode->requestHeaderSize == strlen(rio->rio_buf))
                && !memcmp(rio->rio_buf, startNode->requestHeader, startNode->requestHeaderSize))
        {   
            pop_node(startNode);
            insert_into_tail(startNode);    // by LRU
            rio_writen(clientfd, startNode->responseHeader, startNode->responseHeaderSize);
            rio_writen(clientfd, startNode->responseBody + MAX_REFERENCE, startNode->responseBodySize);
            printf("---Cache hit!!---\n");
            printf("LList-size: %d count: %d\n", LList->size, LList->cnt);
            READUNLOCK();
            return -1;
        }
        startNode = startNode->right;
    }
    printf("---Cache miss!!---\n");
    READUNLOCK();

    strcpy(hostname, "Host: ");
    if ((serverfd = parse_header(clientfd, hostname, method, uri)) == -1)
        return -1;

    while(strcmp(buf, "\r\n")) {
        Rio_readlineb(rio, buf, MAXLINE);
        // check existance of important request headers
        if (strstr(buf, "Host"))
            isHost = 1;
        if (strstr(buf, "User-Agent"))
            isUserAgent = 1;

        if (strstr(buf, "Proxy-Connection"))
            isProxCon = 1;
        else if (strstr(buf, "Connection"))
            isCon = 1;

        if (strcmp(buf, "\r\n"))
        {
            printf("%s", buf);
            rio_writen(serverfd, buf, strlen(buf));
        }
    }

    // insert important missing headers
    if (!isHost) {
        printf("%s", hostname);
        rio_writen(serverfd, hostname, strlen(user_agent_hdr));
    } 
    if (!isUserAgent) {
        printf("%s", user_agent_hdr);
        rio_writen(serverfd, (void *)user_agent_hdr, strlen(user_agent_hdr));
    }
    if (!isCon) {
        strcpy(con, "Connection: close\r\n");
        printf("%s", con);
        rio_writen(serverfd, con, strlen(con));
    }
    if (!isProxCon) {
        strcpy(proxcon, "Proxy-Connection: close\r\n");
        printf("%s", proxcon);
        rio_writen(serverfd, proxcon, strlen(proxcon));
    }
    rio_writen(serverfd, buf, strlen(buf)); // insert "\r\n" at the end of header 
    return serverfd;
}
/* parse request header line to connect server */
int parse_header(int clientfd, char *hostname, char *method, char *uri)
{   
    int serverfd; // ishttps = 0;
    char serverhost[MAXLINE], serverport[MAXLINE], new_uri[MAXLINE];

    // parsing header request, each ptr indicate host, port and filename
    char *hostptr, *portptr, *rootptr;
    hostptr = index(uri, ':') + 3;
    // if (*(hostptr-4) == 's')
    //     ishttps = 1;    
    rootptr = index(hostptr, '/');

    // exception for html src request
    if (rootptr == NULL){
        clienterror(clientfd, "last of address '/' needed", "400", "Bad request", "Proxy have to add '/' at last address");
        return -1;
    }
    
    *rootptr = '\0';
    strcat(hostname, hostptr);
    strcat(hostname, "\r\n");
    portptr = index(hostptr, ':');
    
    if (portptr){
        strcpy(serverport, portptr + 1);
        *portptr = '\0';
    }
    // else if(ishttps)    // https default port
    //     strcpy(serverport, "443");
    else                // http default port
        strcpy(serverport, "80");
    strcpy(serverhost, hostptr);

    // concatenate uri to make new request
    strcpy(new_uri, method);
    strcat(new_uri, " /");
    if (!strcmp(rootptr, "\0"))
        strcat(new_uri, rootptr+1);
    strcat(new_uri, " ");
    strcat(new_uri, "HTML/1.0\r\n");   // modify version HTML/1.1 into HTML/1.0
    
    if ((serverfd = open_clientfd((char *)serverhost, (char *)serverport)) < 0){
        sprintf(serverhost, "%s:%s", serverhost, serverport);
        clienterror(clientfd, serverhost, "403", "Forbidden", "This URI is not allowed Access");
        return -1;
    }
    rio_writen(serverfd, new_uri, strlen(new_uri));
    return serverfd;
}



/* get response from server and send it to client with caching response */
void response_transfer(int clientfd, int serverfd, rio_t *rio)
{   
    char responseHeader[MAXLINE], *buf;
    int read_cnt;
    int responseBodySize = 0, responseHeaderSize = 0;
    rio_t rp;

    // find Content-length in response header
    Rio_readinitb(&rp, serverfd);
    do {
        buf = responseHeader + responseHeaderSize;
        read_cnt = Rio_readlineb(&rp, buf, MAXLINE);
        rio_writen(clientfd, buf, read_cnt);
        if (errno == EPIPE)
            return;
        responseHeaderSize += read_cnt;
        if (strstr(buf, "Content-length"))
            responseBodySize = atoi(index(buf, ':') + 2);
    } while (strcmp(buf, "\r\n"));
    
    if (responseBodySize == 0){ 
        clienterror(clientfd, "rejected access", "404", "Forbidden", "Proxy could not access request file");
        return ;
    }

    if (responseBodySize <= MAX_OBJECT_SIZE)
    {   
        // writer sync
        P(&w);
        // initial Node to save resonse header, body and request header(key)
        node_l *newNode = (node_l *)malloc(sizeof(node_l));
        newNode->requestHeader = (char *)malloc(strlen(rio->rio_buf));
        newNode->responseHeader = (char *)malloc(responseHeaderSize);
        newNode->responseBody = (char *)malloc(MAX_REFERENCE + responseBodySize);
        newNode->requestHeaderSize = strlen(rio->rio_buf);
        newNode->responseHeaderSize = responseHeaderSize;
        newNode->responseBodySize = responseBodySize;

        // copy response header and body
        strncpy(newNode->requestHeader, rio->rio_buf, newNode->requestHeaderSize);
        strncpy(newNode->responseHeader, responseHeader, responseHeaderSize);
        char refer_cnt[MAX_REFERENCE];
        sprintf(refer_cnt, "%09d\n", 1);       // record reference count 
        strcpy(newNode->responseBody, refer_cnt);
        buf = newNode->responseBody + MAX_REFERENCE;
        while ((read_cnt = Rio_readnb(&rp, buf, responseBodySize)) > 0) {
            rio_writen(clientfd, buf, read_cnt);
            buf += read_cnt;
        }

        // check cache overlapping and refer together with other node to reduce cache memory
        node_l *lastNode = LList->tail;
        while (lastNode != NULL) {
            // compare response of cache datas
            char *lastNodeStart = lastNode->responseBody + MAX_REFERENCE;
            char *newNodeStart = newNode->responseBody + MAX_REFERENCE;
            if ((lastNode->responseBodySize == responseBodySize)
                    && !memcmp(lastNodeStart, newNodeStart, responseBodySize))
            {   // entirely overlapping case(both header and body)
                if ((lastNode->requestHeaderSize == newNode->requestHeaderSize) 
                    && !memcmp(lastNode->requestHeader, newNode->requestHeader, newNode->requestHeaderSize)) {
                    free_node(newNode);
                    printf("---Cache totally overlapped!!---\n");
                }
                else // partialy overlapping case(only body)
                {
                    free(newNode->responseBody);
                    up_down_refercnt(lastNode, PLUS);
                    newNode->responseBody = lastNode->responseBody;
                    insert_into_tail(newNode);
                    printf("---Cache body overlapped!!---\n");
                }   
                printf("LList-size: %d count: %d\n", LList->size, LList->cnt);
                V(&w);
                return;
            }
            lastNode = lastNode->left;
        }

        LList->size += responseBodySize;    
        // delete old cache memory while exceeding limit 
        if (LList->size > MAX_CACHE_SIZE)
        {   
            node_l *startNode = LList->head;
            while (LList->size > MAX_CACHE_SIZE) {
                LList->cnt--;
                LList->size -= startNode->responseBodySize;
                printf("Deleted cache size: %d\n", startNode->responseBodySize);
                node_l *nextNode = startNode->right;
                pop_node(startNode);
                free_node(startNode);
                startNode = nextNode;
            }
        }
        insert_into_tail(newNode);
        LList->cnt++;
        printf("LList-size: %d count: %d\n", LList->size, LList->cnt);
        V(&w);
    }
    else
    {   // not suitable size for caching
        printf("It is too big Size to cache: %d\n", responseBodySize);
        char responsebuf[responseBodySize];
        while ((read_cnt = Rio_readlineb(&rp, responsebuf, responseBodySize)) > 0) {
            rio_writen(clientfd, responsebuf, read_cnt);
            if (errno == EPIPE)
                break;
        }
    }
    printf("Response-length: %d\n", responseBodySize);
    Close(serverfd);
}

/* insert the newNode into the tail of linkedlist by LRU */
void insert_into_tail(node_l *newNode)
{
    newNode->right = NULL;
    newNode->left = LList->tail;
    if (newNode->left != NULL)
        newNode->left->right = newNode; 
    LList->tail = newNode;
    if (LList->head == NULL){
        LList->head = newNode;
    }
}
/* pop node from linkedlist */
void pop_node(node_l *popNode)
{
    if (popNode->left != NULL)
        popNode->left->right = popNode->right;
    else 
        LList->head = popNode->right;

    if (popNode->right != NULL)
        popNode->right->left = popNode->left;
    else 
        LList->tail = popNode->left;
}

void free_LList()
{   
    printf("Delete all linked list\n");
    node_l *startNode = LList->head;
    while (startNode != NULL) {
        node_l *nextNode = startNode->right;
        free_node(startNode);
        startNode = nextNode;
    }
    free(LList);
    exit(0);
}

void free_node(node_l *deleteNode)
{   
    printf("Deleted cache size: %d\n", deleteNode->responseBodySize + deleteNode->responseHeaderSize);
    free(deleteNode->requestHeader);
    free(deleteNode->responseHeader);
    int refer_cnt = atoi(deleteNode->responseBody);
    if (refer_cnt == 1)
        free(deleteNode->responseBody);
    else {
        up_down_refercnt(deleteNode, MINUS);
    }
    free(deleteNode);
}
/* manipulating reference count which is 10 length of sting */
void up_down_refercnt(node_l *updateNode, dir direction)
{   
    char refer_cnt[MAX_REFERENCE] = {0};
    if (direction == PLUS) {
        sprintf(refer_cnt, "%09d\n", atoi(updateNode->responseBody) + 1);    
    } else if (direction == MINUS) {
        sprintf(refer_cnt, "%09d\n", atoi(updateNode->responseBody) - 1);    
    }
    strncpy(updateNode->responseBody, refer_cnt, MAX_REFERENCE);
}

/* error msg for exception */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
    char buf[MAXLINE], body[MAXBUF];
    sprintf(body, "\r\n<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The proxy server</em>\r\n", body);
    
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));
}

/* $end proxy.c */