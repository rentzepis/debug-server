/*
 * Student ID: 2012-11598
 * Name: 민두기
 *
 * Cache는 LRU를 queue를 사용하여 구현하였다. 
 * Multithreading을 사용하여 concurrency를 만족하였고,
 * Lazy Deletion 기법을 사용하여 queue의 synchronization을 
 * 구현하였다.
 */
#include <stdio.h>
#include <signal.h>
#include <pthread.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct cache {
    char *data;
    char *uri;
    int valid;
    struct cache *next;
    pthread_mutex_t c_lock;
};

struct cache head, tail;
int total_cache_size = 0;
pthread_mutex_t s_lock;

void init_cache(void);
void create_cache(char *buf, char *uri);
int find_cache(struct cache **cache, char *uri);
int insert_cache(struct cache *cache);
int evict_cache(void);
void delete_cache(void);
void traverse_cache(void);
int validate(struct cache *pred, struct cache *curr);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void construct_requesthdrs(rio_t *rp, char *buf, char *filename, char *uri,
							char *host, char *port);
void parse_uri(char *uri, char *host, char *port, char *filename);
void *doit_thread(void *vargp);

int main(int argc, char *argv[])
{
    int listenfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    init_cache();
    signal(SIGPIPE, SIG_IGN);

    /* Check command line args */
    if (argc != 2) {
	fprintf(stderr, "usage: %s <port>\n", argv[0]);
	exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
	int *connfdp = Malloc(sizeof(int));
	clientlen = sizeof(clientaddr);
	*connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
	Pthread_create(&tid, NULL, doit_thread, connfdp);
    }
    delete_cache();
    return 0;
}

void *doit_thread(void *vargp)
{
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    traverse_cache();
    return NULL;
}

void doit(int fd)
{
    int clientfd;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE];
    char host[MAXLINE], port[MAXLINE];
    char filename[MAXLINE];
    char cache_buf[MAX_OBJECT_SIZE];
    size_t buflen;
    struct cache *cache;
    int cache_size = 0, cache_flag = 1;
    rio_t c_rio, s_rio;

    /* Read request line and headers */
    Rio_readinitb(&c_rio, fd);
    if (!Rio_readlineb(&c_rio, buf, MAXLINE))
	return;
    printf("%s", buf);

    sscanf(buf, "%s %s", method, uri);
    if (strcasecmp(method, "GET")) {
	printf("this proxy can handle only \"GET\"\n");
	return;
    }
    construct_requesthdrs(&c_rio, buf, filename, uri, host, port);

    if (find_cache(&cache, uri) == 1) {
	Rio_writen(fd, cache->data, strlen(cache->data));
    } else {
	clientfd = Open_clientfd(host, port);
	if (clientfd < 0) {
	    printf("there is no such server\n");
	    return;
	}
	Rio_readinitb(&s_rio, clientfd);
	Rio_writen(clientfd, buf, strlen(buf));

	while ((buflen = Rio_readlineb(&s_rio, buf, MAXLINE)) != 0) {
	    Rio_writen(fd, buf, buflen);
	    if (cache_size + buflen > MAX_OBJECT_SIZE) {
		cache_flag = 0;
	    } else {
		memcpy(cache_buf + cache_size, buf, buflen);
		cache_size += buflen;
	    }
	}
	if (cache_flag == 1)
	    create_cache(cache_buf, uri);
	Close(clientfd);
    }
}

void construct_requesthdrs(rio_t *rp, char *buf, char *filename, char *uri,
							char *host, char *port)
{
    char temp[MAXLINE], temp2[MAXLINE];
    int h_flag = 0, c_flag = 0;

    sscanf(buf, "%s %s", temp, uri);

    /* parsing the uri */
    parse_uri(uri, host, port, filename);
    sprintf(buf, "%s %s %s", temp, filename, "HTTP/1.0\r\n");
    sprintf(uri, "%s %s", host, filename);

    /* read header */
    strcat(buf, user_agent_hdr);
    while (strcmp(temp, "\r\n")) {
	Rio_readlineb(rp, temp, MAXLINE);
	printf("%s", temp);
	sscanf(temp, "%s", temp2);
	
	if (!strcmp(temp2, "User-Agent:"))
	    continue;
	if (!strcmp(temp2, "Host:"))
	    h_flag = 1;
	if (strcmp(temp2, "Connection:") && strcmp(temp2, "Proxy-Connection:")) {
	    strcat(buf, temp);
	} else if (c_flag == 0) {
	    sprintf(buf, "%sConnection: close\r\n", buf);
	    sprintf(buf, "%sProxy-Connection: close\r\n", buf);
	    c_flag = 1;
	}
	
	if (!strcmp(temp, "\r\n")) {	
	    if (h_flag == 0) {
		sprintf(buf, "%sHost: %s\r\n", buf, host);
	    }
	    if (c_flag == 0) {
		sprintf(buf, "%sConnection: close\r\n", buf);
		sprintf(buf, "%sProxy-Connection: close\r\n", buf);
	    }
	    strcat(buf, temp);
	}
    }
    printf("%s", buf);
}

void parse_uri(char *uri, char *host, char *port, char *filename)
{
    char *ptr;
    char temp_uri[MAXLINE];

    strcpy(temp_uri, uri);
    ptr = temp_uri + 7;
    while (*ptr != '/' && *ptr != ':') {
	*host = *ptr;
	ptr++;
	host++;
    }
    *host = '\0';

    /* port번호 존재 한다면 가져옴 */
    if (*ptr == ':') {
	ptr++;
	while (*ptr != '/') {
	    *port = *ptr;
	    ptr++;
	    port++;
	}
	*port = '\0';
    } else {
	strcpy(port, "80");
    }
    
    /* filename 가져옴 */
    sscanf(ptr, "%s", filename);
}

void init_cache(void)
{
    head.next = &tail;
    tail.next = &head;
    head.valid = 1;
    tail.valid = 1;
    pthread_mutex_init(&head.c_lock, NULL);
    pthread_mutex_init(&tail.c_lock, NULL);
    pthread_mutex_init(&s_lock, NULL);
}

void create_cache(char *buf, char *uri)
{
    struct cache *cache = (struct cache *)Malloc(sizeof(struct cache));

    pthread_mutex_init(&cache->c_lock, NULL);
    cache->data = (char *)Malloc(strlen(buf));
    cache->uri = (char *)Malloc(strlen(uri));
    cache->valid = 1;

    strcpy(cache->data, buf);
    strcpy(cache->uri, uri);
    
    pthread_mutex_lock(&s_lock);
    total_cache_size += strlen(cache->data);
    pthread_mutex_unlock(&s_lock);

    while (total_cache_size > MAX_CACHE_SIZE)
	evict_cache();

    while (!insert_cache(cache));
}

int validate(struct cache *pred, struct cache *curr)
{
    return pred->valid && curr->valid && pred->next == curr;
}

int find_cache(struct cache **cache, char *uri)
{
    struct cache *temp = &head;
    int flag = 0;

    while (temp->next != &tail) {
	if (!strcmp(temp->next->uri, uri)) {
	    struct cache *target = temp->next;

	    if (target->next != &tail) {
		pthread_mutex_lock(&temp->c_lock);
		pthread_mutex_lock(&target->c_lock);

		if (validate(temp, target)) {
		    if (strcmp(target->uri, uri)) {
			flag = 0;
		    } else {
			temp->next = target->next;
			*cache = target;
			target->valid = 0;
			flag = 1;
		    }
		}

		pthread_mutex_unlock(&target->c_lock);
		pthread_mutex_unlock(&temp->c_lock);
		
		if (flag == 1) {
		    target->valid = 1;
		    while (!insert_cache(target));
		    break;
		}
	    } else {
		if (validate(temp, target)) {
		    *cache = target;
		    flag = 1;
		    break;
		}
	    }
	}
	temp = temp->next;
    }
    return flag;
}

int insert_cache(struct cache *cache)
{
    struct cache *temp = tail.next;
    int flag = 0;

    pthread_mutex_lock(&tail.c_lock);
    pthread_mutex_lock(&temp->c_lock);

    if (validate(&tail, temp) && validate(temp, &tail)) {
	cache->next = &tail;
	tail.next->next = cache;
	tail.next = cache;
	flag = 1;
    }

    pthread_mutex_unlock(&temp->c_lock);
    pthread_mutex_unlock(&tail.c_lock);

    return flag;
}

int evict_cache(void)
{
    struct cache *temp = head.next;
    int flag = 0;

    pthread_mutex_lock(&head.c_lock);
    pthread_mutex_lock(&temp->c_lock);

    if (validate(&head, temp)) {
	head.next = temp->next;
	temp->valid = 0;
	pthread_mutex_lock(&s_lock);
	total_cache_size -= strlen(temp->data);
	pthread_mutex_unlock(&s_lock);
	flag = 1;
    }

    pthread_mutex_unlock(&temp->c_lock);
    pthread_mutex_unlock(&head.c_lock);

    if (flag == 1) {
	Free(temp->data);
	Free(temp->uri);
	Free(temp);
    }
    return flag;
}

void delete_cache(void)
{
    while (head.next != &tail)
	evict_cache();
}

void traverse_cache(void)
{
    struct cache *temp = head.next;
    while (temp != &tail) {
	printf("%s\n", temp->uri);
	temp = temp->next;
    }
}
