#include "csapp.h"


#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

typedef struct cache_line {
    char *uri;           
    char *object;        
    size_t object_size;  
    struct cache_line *next; 
    struct cache_line *prev; 
    struct timeval timestamp; 
} cache_line_t;

typedef struct {
    size_t total_size;  
    cache_line_t *head; 
    cache_line_t *tail; 
    sem_t sem;
} cache_t;

cache_t cache; 


void cache_init();
int cache_lookup(char *uri, char *object, size_t *size);
void cache_uri(char *uri, char *object, size_t obj_size);

void *thread(void *vargp);
void doit(int fd);
void parse_uri(char *uri, char *hostname, char *path, int *port);
void http_header(char *http_header, char *hostname, rio_t *client_rio, char *path);
int connect_toserver(char *hostname, int port);

int main(int argc, char **argv) {
    int listenfd, *connfdp;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    cache_init();

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        Pthread_create(&tid, NULL, thread, connfdp);
    }
    return 0;
}



void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

void print_cache() {
    sem_wait(&cache.sem);
    printf("=== Cache dump (total_size=%zu) ===\n", cache.total_size);
    for (cache_line_t *p = cache.head; p; p = p->next) {
        printf("  %s : %zu bytes\n", p->uri, p->object_size);
    }
    printf("=== end cache ===\n");
    sem_post(&cache.sem);
}


void doit(int fd) {
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], path[MAXLINE];
    char object_buf[MAX_OBJECT_SIZE];
    int port;
    size_t object_size = 0;
    rio_t rio, server_rio;
    
    Rio_readinitb(&rio, fd);
    if (!Rio_readlineb(&rio, buf, MAXLINE))
        return;
    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    
    if (strcasecmp(method, "GET")) {
        printf("Proxy does not implement this method\n");
        return;
    }
    
    parse_uri(uri, hostname, path, &port);
    char canonical_uri[MAXLINE];
    sprintf(canonical_uri, "http://%s:%d%s", hostname, port, path);
    
    size_t cached_size;
    if (cache_lookup(canonical_uri, object_buf, &cached_size)) {
        Rio_writen(fd, object_buf, cached_size);
        return;
    }
    
    int serverfd = connect_toserver(hostname, port);
    if (serverfd < 0) {
        printf("Connection failed\n");
        return;
    }
    
    char request_hdr[MAXLINE];
    http_header(request_hdr, hostname, &rio, path);
    Rio_readinitb(&server_rio, serverfd);
    Rio_writen(serverfd, request_hdr, strlen(request_hdr));
    
    char response_buf[MAX_OBJECT_SIZE];
    size_t n;
    size_t total_size = 0;
    
    while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        printf("Proxy received %d bytes from server\n", (int)n);
        
        if (total_size + n <= MAX_OBJECT_SIZE) {
            memcpy(response_buf + total_size, buf, n);
            total_size += n;
        } else {
            total_size = MAX_OBJECT_SIZE + 1;
        }
        
        Rio_writen(fd, buf, n);
    }
    
    if (total_size <= MAX_OBJECT_SIZE) {
        cache_uri(canonical_uri, response_buf, total_size);
    }
    
    Close(serverfd);
}


void parse_uri(char *uri, char *hostname, char *path, int *port) {
    char buf[MAXLINE];
    char *hoststart, *pathstart, *portstart;

    strcpy(buf, uri);
    *port = 80; 

    if ((hoststart = strstr(buf, "://")) != NULL)
        hoststart += 3;
    else
        hoststart = buf;

    if ((pathstart = strchr(hoststart, '/')) != NULL) {
        strcpy(path, pathstart);
        *pathstart = '\0';
    } else {
        strcpy(path, "/");
    }

    if ((portstart = strchr(hoststart, ':')) != NULL) {
        *portstart = '\0';
        *port = atoi(portstart + 1);
    }

    strcpy(hostname, hoststart);
}


int connect_toserver(char *hostname, int port) {
    char port_str[10];
    sprintf(port_str, "%d", port);
    return Open_clientfd(hostname, port_str);
}

void http_header(char *http_header, char *hostname , rio_t *client_rio , char *path) {
    char buf[MAXLINE], request_hdr[MAXLINE], host_hdr[MAXLINE], others[MAXLINE];
    
    others[0] = '\0';
    host_hdr[0] = '\0';
    
    sprintf(request_hdr, "GET %s HTTP/1.0\r\n", path);
    
    while (Rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0) break;  
        
        if (!strncasecmp(buf, "Host:", 5)) {
            strcpy(host_hdr, buf);
            continue;
        }
        
        if (!strncasecmp(buf, "Connection:", 11) ||
            !strncasecmp(buf, "Proxy-Connection:", 17) ||
            !strncasecmp(buf, "User-Agent:", 11)) {
            continue;
        }
        
        strcat(others, buf);
    }
    
    if (strlen(host_hdr) == 0) {
        sprintf(host_hdr, "Host: %s\r\n", hostname);
    }
    
    
    sprintf(http_header, "%s%s%s%s%s%s\r\n", 
            request_hdr, host_hdr, user_agent_hdr, 
            connection_hdr, proxy_connection_hdr, others);
}


void cache_init() {
    cache.head = NULL;
    cache.tail = NULL;
    cache.total_size = 0;
    sem_init(&cache.sem, 0, 1);
}


int cache_lookup(char *uri, char *object, size_t *size) {
    sem_wait(&cache.sem);

    cache_line_t *p = cache.head;
    while (p) {
        if (strcmp(p->uri, uri) == 0) {
            memcpy(object, p->object, p->object_size);
            *size = p->object_size;

            if (p != cache.head) {
                if (p->prev) 
                    p->prev->next = p->next;

                if (p->next) 
                    p->next->prev = p->prev;

                if (p == cache.tail) 
                    cache.tail = p->prev;

                p->next = cache.head;
                p->prev = NULL;
                cache.head->prev = p;
                cache.head = p;
            }

            sem_post(&cache.sem);
            printf("Cache hit: %s ----> %zu bytes\n", uri, *size);
            return 1;
        }
        p = p->next;
    }

    sem_post(&cache.sem);
    printf("Cache miss: %s\n", uri);
    return 0;
}


void cache_uri(char *uri, char *object, size_t obj_size) {
    if (obj_size > MAX_OBJECT_SIZE) return;  
    
    sem_wait(&cache.sem);
    
    
    while (cache.total_size + obj_size > MAX_CACHE_SIZE) {
        if (!cache.tail) break;  
        cache_line_t *evict = cache.tail;
        
        
        cache.tail = evict->prev;
        if (cache.tail) cache.tail->next = NULL;
        else cache.head = NULL;
        
        
        cache.total_size -= evict->object_size;
        Free(evict->uri);
        Free(evict->object);
        Free(evict);
        
        printf("Deleted cache item\n");
    }
    
    
    cache_line_t *new_entry = Malloc(sizeof(cache_line_t));
    new_entry->uri = Malloc(strlen(uri) + 1);
    strcpy(new_entry->uri, uri);
    
    new_entry->object = Malloc(obj_size);
    memcpy(new_entry->object, object, obj_size);
    new_entry->object_size = obj_size;
    
    gettimeofday(&new_entry->timestamp, NULL);
    
    
    new_entry->next = cache.head;
    new_entry->prev = NULL;
    if (cache.head) cache.head->prev = new_entry;
    cache.head = new_entry;
    if (!cache.tail) cache.tail = new_entry;
    
    cache.total_size += obj_size;
    
    sem_post(&cache.sem);
    printf("Added to cache: %s ---> %zu bytes\n", uri, obj_size);
    print_cache();
}