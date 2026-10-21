/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 */

/* Some useful includes to help you get started */

#include "csapp.h"
#include "cache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdbool.h>
#include <inttypes.h>
#include <unistd.h>
#include <assert.h>

#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>


/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
//#define MAX_CACHE_SIZE (1024*1024)
#define MAX_OBJECT_SIZE (100*1024)
#define HOSTLEN 256
#define SERVLEN 8

typedef struct sockaddr SA;

void *thread_routine(void *vargp);
void doit(int connfd);
bool read_request_headers(rio_t *rp, char* headers, char *hostname);
void parse_uri(char *uri, char *hostname, char *port, char *path);
bool proxy(int serverfd, int connfd, char *method, char *path, char *headers, char *uri);

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                    " (X11; Linux x86_64; rv:3.10.0)"
                                    " Gecko/20190801 Firefox/63.0.1";
static pthread_mutex_t mutex;
static queue *cache;

int main(int argc, char** argv) {
	struct sockaddr_in addr;
	socklen_t addrlen; 
	char host[HOSTLEN], serv[SERVLEN];
	int *connfd;
	pthread_t tid;

    /* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }

    pthread_mutex_init(&mutex, NULL);
    cache = create_cache(); 
    signal(SIGPIPE, SIG_IGN);
    int listenfd = open_listenfd(argv[1]);

    while (1) {
    	addrlen = sizeof(addr);
    	connfd = Malloc(sizeof(int));

    	if ((*connfd = accept(listenfd, (SA *)&addr, &addrlen)) < 0) {
    		free(connfd);
			continue;
    	}

    	int res = getnameinfo((SA *)&addr, addrlen, host, HOSTLEN, 
			serv, SERVLEN, 0);
		if (res != 0)
		fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));

    	pthread_create(&tid, NULL, thread_routine, connfd);                                                                              
    }
    free_cache(cache);
    return 0;
}

void *thread_routine(void *vargp) {
	int connfd = *(int *)vargp;
	pthread_detach(pthread_self());
	free(vargp);
	doit(connfd);
	return NULL;
}

void doit(int connfd) {
	rio_t rio;
	rio_readinitb(&rio, connfd);

	char buf[MAXLINE];
	if (rio_readlineb(&rio, buf, MAXLINE) <= 0)
		return;

	char method[MAXLINE], uri[MAXLINE], version;
	if (sscanf(buf, "%s %s HTTP/1.%c", method, uri, &version) != 3 || (version != '0' && version != '1'))
		return;

	if (strncmp(method, "GET", sizeof("GET"))) 
		return;

	pthread_mutex_lock(&mutex);
	node *cache_node = extract_from_cache(cache, uri);
	pthread_mutex_unlock(&mutex);

//sio_printf("the uri is: %s\n", uri);
//node *current = cache->head;
//while(current != NULL) {
//  sio_printf("%s\n", current->key);
//  current = current->next;

//}

	//found in cache
	if (cache_node != NULL) {
	//sio_printf("found--------------\n");
		cache_node->is_transmitting = true;
		rio_writen(connfd, (void *)cache_node->object, cache_node->node_size);
		cache_node->is_transmitting = false;
	}
	//not found in cache
	else {
char key[MAXLINE];
strncpy(key, uri, strlen(uri));
		char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
		parse_uri(uri, hostname, port, path);

		char headers[MAXLINE] = "";
		if (read_request_headers(&rio, headers, hostname))
			return;

		int serverfd = open_clientfd(hostname, port);
		if (serverfd == -1)
			return;

		if (proxy(serverfd, connfd, method, path, headers, key)) {
			close(serverfd);
			return;
		}
		close(serverfd);
	}
	close(connfd);
}

/*
 * read_request_headers - read HTTP request headers
 * Returns true if an error occurred, or false otherwise.
 */
bool read_request_headers(rio_t *rp, char* headers, char *hostname) {
	char buf[MAXLINE];
	bool is_host_header = false;
	if (rio_readlineb(rp, buf, MAXLINE) == -1)
		return true;

	while (strncmp(buf, "\r\n", sizeof("\r\n"))) {
		if (strstr(buf, "Host:") != NULL) {
			is_host_header = true;
			strcat(headers, buf);
		}
		else if (strstr(buf, "User-Agent:") != NULL) {}
		else if (strstr(buf, "Proxy-Connection:") != NULL) {}
		else if (strstr(buf, "Connection:") != NULL) {}
		else 
			strcat(headers, buf);

		if (rio_readlineb(rp, buf, MAXLINE) == -1)
			return true;
	}

	if (is_host_header == false) {
		char host_header[MAXLINE];
		sprintf(host_header, "Host:%s\r\n", hostname);
		strcat(headers, host_header);
	}

	char compulsory_headers[MAXLINE];
	sprintf(compulsory_headers, "User-Agent:%s\r\nProxy-Connection: close\r\nConnection: close\r\n\r\n", header_user_agent);
	strcat(headers, compulsory_headers);
    return false;
}

void parse_uri(char *uri, char *hostname, char *port, char *path) {
	char *p, *saveptr;
    strcpy(path, strchr(uri+7, '/'));
    p = strchr(uri+7, '/');
    *p = '\0';
    strcpy(hostname, __strtok_r(uri+7, ":", &saveptr));
    if((p = __strtok_r(NULL, ":", &saveptr)) == NULL)
        strcpy(port, "80");
    else
        strcpy(port, p);
}

bool proxy(int serverfd, int connfd, char *method, char *path, char *headers, char *uri) {
	rio_t rio;
	rio_readinitb(&rio, serverfd);

	char buf_sent[MAXLINE], buf_rec[MAXLINE];
	sprintf(buf_sent, "%s %s HTTP/1.0\r\n%s", method, path, headers);
	if (rio_writen(serverfd, (void *)buf_sent, strlen(buf_sent)) == -1) 
		return true;

	int readn;
	size_t response_size = 0;
	char response[MAX_CACHE_SIZE];
	while ((readn = rio_readnb(&rio, (void *)buf_rec, MAXLINE)) != 0) {
		//if (rio_writen(connfd, (void *)buf_rec, readn) == -1)
		//	return true;

		//accumulate total response
		
		//if (response_size <= MAX_OBJECT_SIZE) 
		memcpy(response + response_size, buf_rec, readn);
		response_size += readn;
	}

	pthread_mutex_lock(&mutex);
	node *cache_node = extract_from_cache(cache, uri);
	pthread_mutex_unlock(&mutex);

	if (cache_node != NULL) {
		cache_node->is_transmitting = true;
		rio_writen(connfd, (void *)cache_node->object, cache_node->node_size);
		cache_node->is_transmitting = false;
	} else {
		rio_writen(connfd, (void *)response, response_size);
	//add uri, object to cache
	if (response_size <= MAX_OBJECT_SIZE) {
		pthread_mutex_lock(&mutex);
		insert_to_cache(cache, uri, response, response_size);
		pthread_mutex_unlock(&mutex);
	}
}

	return false;
}
















