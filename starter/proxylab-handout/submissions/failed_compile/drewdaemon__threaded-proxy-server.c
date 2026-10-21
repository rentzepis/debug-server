#include "csapp.h"
#include "cache.h"
#include "sbuf.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NCONS 23
#define NLOGS 23
#define NTHREADS 8

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
char cached_path[MAXLINE];
unsigned char cached_obj[MAX_OBJECT_SIZE];
int cached_size = 0;
sbuf_t connection_buffer;
sbuf_t logging_buffer;

void doit(int fd);
void read_requesthdrs(rio_t *rp);
void parse_uri(char *uri, char *hostname, char *path, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void build_and_send_request(int conn, int fd, rio_t *rp, char *hostname, char *port, char *path, char *response);
void *thread(void *argp);
void *logger(void *argp);

int main(int argc, char **argv)
{
	int listenfd;
	pthread_t tid;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	clientlen = sizeof(clientaddr);

	/* Check command line args */
	if (argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}
	listenfd = Open_listenfd(argv[1]);
	int connfd;

	sbuf_init(&connection_buffer, NCONS);
	sbuf_init(&logging_buffer, NLOGS);

	for (int i = 0; i < NTHREADS; i++) /* Create worker threads */
		Pthread_create(&tid, NULL, thread, NULL);

	// Logging thread
	Pthread_create(&tid, NULL, logger, NULL);

	cache_init();

	while (1)
	{
		connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
					port, MAXLINE, 0);
		sbuf_insert(&connection_buffer, (char *)connfd);
	}
}

void *thread(void *argp)
{
	Pthread_detach(pthread_self());
	while (1)
	{
		int fd = (int)sbuf_remove(&connection_buffer);
		doit(fd);
	}
	return NULL;
}

void *logger(void *argp)
{
	Pthread_detach(pthread_self());
	FILE *fp;
	fp = fopen("server.log", "a");
	while (1)
	{
		char *logline = sbuf_remove(&logging_buffer);
		printf("%s\n", logline);
		fprintf(fp, "%s", logline);
		fflush(fp);
	}
	return NULL;
}

void doit(int fd)
{
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE],
		hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
	rio_t rio;

	/* Read request line and headers */
	Rio_readinitb(&rio, fd);
	if (!Rio_readlineb(&rio, buf, MAXLINE))
		return;

	sscanf(buf, "%s %s %s", method, uri, version);
	if (strcasecmp(method, "GET"))
	{
		clienterror(fd, method, "501", "Not Implemented",
					"Proxy does not implement this method");
		return;
	}

	parse_uri(uri, hostname, port, path);

	/* still need to check for Host */

	if (!*port)
	{
		strcpy(port, "80");
	}
	if (!*hostname)
	{
		strcpy(hostname, "www.facebook.com");
	}

	cache_print();

	unsigned char *cached_object;
	int *object_size = malloc(sizeof(int));
	if ((cached_object = cache_get_object(path, object_size)))
	{
		printf("CACHED %d\n", *object_size);
		// printf("%s", cached_object);
		send(fd, cached_object, *object_size, 0);
	}
	else
	{
		printf("NOT CACHED\n");
		int conn = Open_clientfd(hostname, port);

		char response[MAXLINE];

		build_and_send_request(conn, fd, &rio, hostname, port, path, response);
		int size = 0;
		*object_size = 0;
		unsigned char *object_to_cache = malloc(sizeof(unsigned char) * MAX_OBJECT_SIZE);
		while ((size = recv(conn, response, MAXLINE, 0)))
		{
			if (*object_size + size < MAX_OBJECT_SIZE)
			{
				memcpy(&object_to_cache[*object_size], response, size);
			}
			*object_size += size;
			send(fd, response, size, 0);
		}
		Close(conn);

		cache_object(path, object_to_cache, *object_size);
		printf("object cached!");
	}
	Close(fd);

	time_t t = time(NULL);
	struct tm tm = *localtime(&t);

	char *logline = malloc(sizeof(char) * MAXLINE);
	sprintf(logline, "[%d-%d-%d %d:%d:%d] %s %s %s %d\n", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, method, uri, version, *object_size);
	sbuf_insert(&logging_buffer, logline);

	memset(port, 0, MAXLINE);
	memset(hostname, 0, MAXLINE);
}

int reservedHeader(char *header_line)
{
	const char s[2] = ":";
	char *temp[MAXLINE];
	char *token;
	strcpy(temp, header_line);

	/* get the first token */
	token = strtok(temp, s);
	return strlen(token) && (!strcmp(token, "Host") ||
							 !strcmp(token, "User-Agent") ||
							 !strcmp(token, "Connection") ||
							 !strcmp(token, "Proxy-Connection"));
}

void build_and_send_request(int conn, int fd, rio_t *rp, char *hostname, char *port, char *path, char *response)
{
	char buf[MAXLINE];

	// printf("\nBEGIN REQUEST\n\n");

	/* Print the HTTP response */
	sprintf(buf, "GET %s HTTP/1.0\r\n", path);
	Rio_writen(conn, buf, strlen(buf));
	// printf("%s", buf);
	sprintf(buf, "Host: %s\r\n", hostname);
	Rio_writen(conn, buf, strlen(buf));
	// printf("%s", buf);
	sprintf(buf, "User-Agent: %s", user_agent_hdr);
	Rio_writen(conn, buf, strlen(buf));
	// printf("%s", buf);
	sprintf(buf, "Connection: close\r\n");
	Rio_writen(conn, buf, strlen(buf));
	// printf("%s", buf);
	sprintf(buf, "Proxy-Connection: close\r\n");
	Rio_writen(conn, buf, strlen(buf));
	// printf("%s", buf);

	/* Send other headers from request */
	Rio_readlineb(rp, buf, MAXLINE);
	if (!reservedHeader(buf))
	{
		Rio_writen(conn, buf, strlen(buf));
		// printf("%s", buf);
	}
	while (strcmp(buf, "\r\n"))
	{
		Rio_readlineb(rp, buf, MAXLINE);
		if (!reservedHeader(buf))
		{
			Rio_writen(conn, buf, strlen(buf));
			// printf("%s", buf);
		}
	}
	// printf("\nEND REQUEST\n");
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
	/* Get past http:// */
	if (*uri == 'h' || *uri == 'H')
	{
		while (*uri != '/')
		{
			uri++;
		}
		uri++;
		uri++;
	}

	int count = 0;
	char *cntptr = uri;
	while (*cntptr && *cntptr != ':' && *cntptr != '/')
	{
		cntptr++;
		count++;
	}

	strncpy(hostname, uri, count);

	uri = cntptr;
	count = 0;

	if (*uri == ':')
	{
		uri++;
		cntptr++;
		while (*cntptr && *cntptr != '/')
		{
			cntptr++;
			count++;
		}
		strncpy(port, uri, count);
	}

	uri = cntptr;
	count = 0;

	if (*uri == '/')
	{
		while (*cntptr)
		{
			cntptr++;
			count++;
		}
		strcpy(path, uri);
	}
}

void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
	char buf[MAXLINE], body[MAXBUF];

	/* Build the HTTP response body */
	sprintf(body, "<html><title>Tiny Error</title>");
	sprintf(body, "%s<body bgcolor="
				  "ffffff"
				  ">\r\n",
			body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}
