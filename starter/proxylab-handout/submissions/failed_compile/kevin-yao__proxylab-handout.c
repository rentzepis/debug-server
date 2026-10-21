/*
 *            Name: Kangping Yao; Andrew ID: kyao
 * This program acts as a proxy between browser and server. It will forward 
 * browser's http "GET"(only GET) request to server and receive web data from
 * server. This program has also implemented cache function. It will save web
 * data with limited size to local cache. And next time if the browser request
 * the same url, the proxy fetches data from cache directly to reduce network  
 * delay.
 */

#include <stdio.h>
#include "csapp.h"
#include "cache.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define HTTP_URL_PREFIX 7

#ifdef DEBUG
#define dbg_printf(...) printf(__VA_ARGS__)
#else
#define dbg_printf(...)
#endif
/* You won't lose style points for including these long lines in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static const char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";
pthread_rwlock_t lock;
void doit(int fd);
int parseUrl(char* url, char* host, int* serverPort, char* uri);
int checkPort(char* ptr);
void addRequestedHeaders(char* httpRequest);
void *thread(void *vargp);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);

//main routine for the proxy		 
int main(int argc, char** argv)
{
    int listenfd, *connfd, port, clientlen;
    struct sockaddr_in clientaddr;
	pthread_t tid;
	/* Check command line args */
    if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }
    if((port = checkPort(argv[1]))<0){
		exit(1);
	}
	pthread_rwlock_init(&lock,NULL);
	listenfd = Open_listenfd(port);
	Signal(SIGPIPE, SIG_IGN);
	//test_cache();
    while (1) {
		clientlen = sizeof(clientaddr);
		connfd = Malloc(sizeof(int));
		*connfd = Accept(listenfd, (SA *)&clientaddr, 
						(socklen_t *)&clientlen);
		Pthread_create(&tid, NULL, thread, connfd);
    }
    return 0;
}
//thread responsible for handle request 
void *thread(void *vargp){
	int connfd = *((int*)vargp);
	Pthread_detach(pthread_self());
	Free(vargp);
	doit(connfd);
	Close(connfd);
	return NULL;
}
//main routine for handling client requests
void doit(int fd){
	char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
	char dataBuf[MAX_OBJECT_SIZE], cacheBuf[MAX_OBJECT_SIZE];
	char httpRequest[MAXLINE], host[MAXLINE], uri[MAXLINE];
	int hostIndex=0, clientfd, dataLength=0, length;
	rio_t rio;
	int serverPort = 80; //default port number
	dataNode *cacheData;
	
	Rio_readinitb(&rio, fd);
	Rio_readlineb(&rio, buf, MAXLINE);
	sscanf(buf, "%s %s %s", method, url, version);
	//only handle GET request
	if (strcasecmp(method, "GET")) { 
       clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
        return;
    }
	
	parseUrl(url, host, &serverPort, uri);
	memset(httpRequest, 0, sizeof(httpRequest));
	strcpy(method, "GET ");
	strcat(httpRequest, method);
	strcat(httpRequest, uri);
	strcpy(version, " HTTP/1.0\r\n");
	strcat(httpRequest, version);
	
	memset(buf, 0, sizeof(buf));
	Rio_readlineb(&rio, buf, MAXLINE);
	//read the rest of request
	while(strcmp(buf, "\r\n")){
		char key[MAXLINE];
		strncpy(key, buf, (strchr(buf, ':') - buf));
		*(key + (strchr(buf, ':') - buf))='\0';
		if(!strcmp(key, "Host")){
			hostIndex = 1;
			strcat(httpRequest, buf);
		//skip these requested header and add later
		}else if(strcmp(key, "User-Agent")&&strcmp(key, "Accept")
				&&strcmp(key, "Accept-Encoding")&&strcmp(key, "Connection")
				&&strcmp(key, "Proxy-Connection")){
			strcat(httpRequest, buf);
		}
		memset(buf, 0, sizeof(buf));
		Rio_readlineb(&rio, buf, MAXLINE);
	}
	//If not add Host header, add it here
	if(!hostIndex){
		strcat(httpRequest, "Host: ");
		strcat(httpRequest, host);
		strcat(httpRequest, "\r\n");
	}
	// Add all required header
	addRequestedHeaders(httpRequest);
	
	pthread_rwlock_wrlock(&lock);
	// Get data from cache
	if((cacheData = getData(url))==NULL){
		dbg_printf("1 url: %s\n", url);
		pthread_rwlock_unlock(&lock);
		clientfd = Open_clientfd_r(host, serverPort);
		Rio_writen(clientfd, httpRequest, strlen(httpRequest));
		Rio_readinitb(&rio, clientfd);
		memset(dataBuf, 0, sizeof(dataBuf));
		while((length=rio_readnb(&rio, dataBuf, MAX_OBJECT_SIZE))>0){
			memcpy(cacheBuf, dataBuf, length);			
			Rio_writen(fd, dataBuf, length);
			memset(dataBuf, 0, sizeof(dataBuf));
			dataLength += length;
		}
		//save data of size less than MAX_OBJECT_SIZE
		//skip data reuqired no cache
		if(dataLength < MAX_OBJECT_SIZE && 
			strstr(cacheBuf, "Cache-Control: no-cache")==NULL){
			pthread_rwlock_wrlock(&lock);
			saveData(url, cacheBuf, dataLength);
			pthread_rwlock_unlock(&lock);
		}
		Close(clientfd);
	}else{
		dbg_printf("2 url: %s\n", url);
		memcpy(cacheBuf, cacheData->data, cacheData->dataLength);
		dataLength = cacheData->dataLength;
		//if find node, put node to head
		pthread_rwlock_unlock(&lock);
		//read data from cache
		Rio_writen(fd, cacheBuf, dataLength);
	}
}


// Add requested header.
void addRequestedHeaders(char* httpRequest){
	strcat(httpRequest, user_agent_hdr);
	strcat(httpRequest, accept_hdr);
	strcat(httpRequest, accept_encoding_hdr);
	strcat(httpRequest, connection_hdr);
	strcat(httpRequest, proxy_connection_hdr);
	strcat(httpRequest, "\r\n");
}

// Extract host, port, uri from url
int parseUrl(char* url, char* host, int* serverPort, char* uri){
	char* ptr;
	int hostLength;	
	url = url + HTTP_URL_PREFIX; //pass http
	ptr = strchr(url, '/');
	hostLength = ptr - url;
	strncpy(host, url, hostLength);
	*(host+hostLength) = '\0';
	
	if((ptr = strchr(host, ':')) != NULL){
		*serverPort = atoi(ptr+1);
		*ptr = '\0';//filter port number 
	}
	
	strcpy(uri, url + hostLength);
		 
	return 1;		 
}

// Check the validation of port argument
int checkPort(char *p){
	char *ptr = p;
	int port;
	while(*ptr != '\0'){
		if(*ptr < '0' || *ptr > '9')
			return -1;
		ptr++;
	}
	port = atoi(p);
	if(port <= 1024 || port >= 65536){
		printf("Port number is beyond legal range\n");
		return -1;
	}
	return port;
}

/*
 * clienterror - returns an error message to the client
 */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
