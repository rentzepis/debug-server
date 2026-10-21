/*
 *	--
 *	--
 *
 *	这是一个简易的网络代理程序，通过多线程实现并发操作。在主函数中，创建一个监听端口，并不断尝试
 *	接受连接。当接受连接之后，创建一个新的线程处理这个连接对应的请求。
 *	首先读客户端发送的请求行和请求报头，接着在缓存系统中寻找是否缓存过对应请求的结果，如果缓存过，则
 *	直接提取出结果返回给客户端，否则，进行进一步的解码分析，提取出服务器名、路径和端口号，尝试连接服
 *	务器，构造对应的请求行和请求报头，向服务器发送请求，等待服务器返回结果，将结果写入缓存，最后将结
 *	返回给客户端。
 *
 *	在缓存层面，使用了类似内存分配器的分级缓存策略，创建了5类缓存块，每个大小分别为1200B, 5200B,
 *	10240B, 25600B, 102400B，各类块的个数分别为20,20,10,10,5。每次在读缓存时搜索所有缓存块
 *	进行字符串比较，写缓存时在对应大小的块写。在多线程方面，采用了读者-写者模型对部分变量加锁。
 */

#include <stdio.h>
#include "csapp.h"
#include "cache.h"

#define MAX_OBJECT_SIZE 102400
#define CACHE
int verbose = 0;

/* You won't lose style points for including this long line in your code */
static const char
	* user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void doit(int fd);
void clienterror(int fd, char* cause, char* errnum,
	char* shortmsg, char* longmsg);
int parse_uri(char* uri, char* hostname, char* path, char* port);
void build_requesthdrs(rio_t* rp, char* newreq, char* hostname, char* port, char* path);
void* thread(void* vargp);

/*
 *	首先初始化缓存体系，创建监听端口，连接客户端成功后，创建新线程处理客户端发送的请求。
 */
int main(int argc, char** argv) {
	int listenfd, * connfd;
	pthread_t tid;
	char hostname[MAXLINE], port[MAXLINE];
	socklen_t clientlen;
	struct sockaddr_storage clientaddr;
	signal(SIGPIPE, SIG_IGN);

	/* Check command line args */
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
	}

	#ifdef CACHE
	init_cache();
	#endif
	listenfd = Open_listenfd(argv[1]);
	while (1) {
		printf("listening..\n");
		clientlen = sizeof(clientaddr);
		connfd = Malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
		Getnameinfo((SA*)&clientaddr, clientlen, hostname, MAXLINE,
			port, MAXLINE, 0);
		printf("Accepted connection from (%s, %s)\n", hostname, port);
		Pthread_create(&tid, NULL, thread, connfd);
	}
	Pthread_join(&tid, NULL);
	close(listenfd);
	#ifdef CACHE
	free_cache();
	#endif
}
/*
 *	线程调用doit处理对应的请求
 */
void* thread(void* vargp) {
	int connfd = *((int*)vargp);
	doit(connfd);
	Close(connfd);
	Free(vargp);
	return NULL;
}
/*
 *	从客户端读取请求行和请求报头，在method, uri, version, buf中存储对应的信息
 */
int read_request_header(int client_fd, rio_t* from_client,
	char* method, char* uri, char* version, char* buf) {
	Rio_readinitb(from_client, client_fd);
	if (Rio_readlineb(from_client, buf, MAXLINE) == 0) {
		printf("Not receive enough data\n");
		return -1;
	}
	sscanf(buf, "%s %s %s", method, uri, version);
	if (strcasecmp(method, "GET") != 0) {
		clienterror(client_fd, method, "501", "Not Implemented",
			"Proxy Server does not implement this method");
		return -1;
	}
	return 0;
}
/*
 *	处理未缓存的客户端请求，向服务器发送请求，接受服务器的响应并将响应写入缓存，最后将响应转发给
 *	客户端。
 */
void Response_Non_Cached(int endserver_fd, char* newreq, rio_t to_endserver,
	char* buf, int client_fd, char* uri) {
	Rio_writen(endserver_fd, newreq, strlen(newreq));
	int n;
	#ifdef CACHE
	int size = 0;
	char data[MAX_OBJECT_SIZE + 1];
	#endif
	while ((n = Rio_readlineb(&to_endserver, buf, MAXLINE))) {
		#ifdef CACHE
		/* If still enough place to cache, store it */
		if (size + n < MAX_OBJECT_SIZE) {
			memcpy(data + size, buf, n);
			size += n;
		}
		#endif
		/* Write response (in buf) to it's client */
		Rio_writen(client_fd, buf, n);
	}
	#ifdef  CACHE
	/* Cache it */
	if (size <= MAX_OBJECT_SIZE) {
		write_cache(uri, data, size);
	}
	#endif
}
/*
 *	处理已经缓存的客户端请求，将从内存中取出来的响应转发给客户端。
 */
void Response_Cached(int endserver_fd, char* cached_data, int data_size) {
	Rio_writen(endserver_fd, cached_data, data_size);
}
/*
 *	处理HTTP请求，首先读客户端发送的请求行和请求报头，接着在缓存系统中寻找是否缓存过对应请求的结果，
 *	如果缓存过，则直接提取出结果返回给客户端，否则，进行进一步的解码分析，提取出服务器名、路径和端口
 *	号，尝试连接服务器，构造对应的请求行和请求报头，向服务器发送请求，等待服务器返回结果，
 *	将结果写入缓存，最后将结返回给客户端。
 */
void doit(int client_fd) {
	int server_fd;
	rio_t client, server;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
	char hostname[MAXLINE], path[MAXLINE], port[10];
	char handout[MAXLINE];
	/* Read request line and request headers */
	if (read_request_header(client_fd, &client, method, uri, version, buf) == -1) {
		return;
	}
	if (verbose) {
		printf("%s %s %s", method, uri, version);
		printf("%s", buf);
	}
	/* Search in cache */
	#ifdef CACHE
	if (Read_Cache_Response(uri, client_fd) == 1) {
		/* Cached request, already responded */
		return;
	}
	#endif
	/* Not in cache, phase uri */
	if (parse_uri(uri, hostname, path, port) == -1) {
		/* Parse uri error */
		clienterror(client_fd, uri, "404",
			"Not found", "Parse uri error");
		return;
	}
	/* Connect to target server */
	if ((server_fd = Open_clientfd(hostname, port)) < 0) {
		printf("Connection failed\n");
		return;
	}
	/* Read end server requestline */
	Rio_readinitb(&server, server_fd);
	/* Build request headers for response to proxy's client */
	build_requesthdrs(&client, handout, hostname, port, path);
	/* Response_Non_Cached to proxy's client */
	Response_Non_Cached(server_fd, handout, server, buf, client_fd, uri);
	Close(server_fd);
}

/*
 *	异常信息通报
 */
void clienterror(int fd, char* cause, char* errnum,
	char* shortmsg, char* longmsg) {
	char buf[MAXLINE], body[MAXBUF];
	/* Build the HTTP response body */
	sprintf(body, "<html><title>Proxy Error</title>");
	sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
	sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
	sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
	sprintf(body, "%s<hr><em>The Proxy Web server</em>\r\n", body);

	/* Print the HTTP response */
	sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-type: text/html\r\n");
	Rio_writen(fd, buf, strlen(buf));
	sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
	Rio_writen(fd, buf, strlen(buf));
	Rio_writen(fd, body, strlen(body));
}
/*
 *	解码uri，提取出服务器名、路径和端口
 */
int parse_uri(char* uri, char* hostname, char* path, char* port) {
	/* uri: http://127.0.0.1.nip.io:16048/cgi-bin/forwarder?63398612 */
	int i = 0, size = strlen(uri), k;
	for (i = 0; i < size; ++i) {
		if (i + 1 < size && uri[i] == '/' && uri[i + 1] == '/')
			break;
	}
	if (i >= size) {
		printf(" ERROR when parse uri: no hostname\n");
		return -1;
	}
	/* uri[i]: 127.0.0.1.nip.io:16048/cgi-bin/forwarder?63398612 */
	i += 2;
	k = 0;
	/* hostname 127.0.0.1.nip.io */
	while (uri[i] != ':' && uri[i] != '\0') {
		hostname[k++] = uri[i++];
	}
	if (i == size) {
		printf(" ERROR when parse uri: no port\n");
		return -1;
	}
	i += 1;
	k = 0;
	/* port 16048 */
	while (isdigit(uri[i])) {
		port[k++] = uri[i++];
	}
	if (i == size) {
		printf(" ERROR when parse uri: no path\n");
		return -1;
	}
	k = 0;
	/* path /cgi-bin/forwarder?63398612 */
	while (uri[i] != '\0') {
		path[k++] = uri[i++];
	}
	if (verbose) {
		printf("%s", uri);
		printf("%s %s %s", hostname, path, port);
	}
	return 0;
}
/*
 *	构建请求报头
 */
void build_requesthdrs(rio_t* rp, char* newreq, char* hostname, char* port, char* path) {
	char buf[MAXLINE];
	const char* str1 = "Connection: close\r\n";
	const char* str2 = "Proxy-Connection: close\r\n";
	memset(newreq, 0, MAXLINE);
	sprintf(newreq, "GET %s HTTP/1.0\r\n", path);
	while (Rio_readlineb(rp, buf, MAXLINE) > 0) {
		if (strcmp(buf, "\r\n") == 0)
			break;
		sprintf(newreq, "%s %s", newreq, buf);
	}
	sprintf(newreq, "%sHost: %s:%s\r\n", newreq, hostname, port);
	sprintf(newreq, "%s %s %s %s\r\n", newreq, user_agent_hdr, str1, str2);
	sprintf(newreq, "%s\r\n", newreq);
	if (verbose) {
		printf("%s", newreq);
	}
}