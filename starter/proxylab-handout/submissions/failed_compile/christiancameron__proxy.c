/**
Christian Cameron 
	Proxy Lab (lab6)
**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "csapp.h"
#include "cacher.h"

void *main_thread(void *varg);
void parse(int cfd);
int foward_request(rio_t *rp, char *nohttp_url);
int foward_response(rio_t *rp, int cfd, char *nohttp_url);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:56.0) Gecko/20100101 Firefox/56.0\r\n";
static const char *prox_conn = "Proxy-Connection: close\r\n";
static const char *conn = "Connection: close\r\n";

int main(int argc, char **argv) {
	char *port;
	pthread_mutex_init(&mutex, NULL);
	cache_init();
	
	// Validate port number
	if(argc != 2) {
		printf("Needs a port number\n");
		exit(2);
	}

	port = argv[1];

	// Proxy file descriptor
	int fd = open_listenfd(port);
	if(fd < 0) {
		perror("Not a valid port number\n");
		exit(2);
	}

	Signal(SIGPIPE, SIG_IGN);

	// Accept connections
	socklen_t client_len;
	struct sockaddr_in *client_addr;
	int *cfd;
	pthread_t tid;
	while(1) {
		printf("Listening for a connection\n");
		cfd = malloc(sizeof(int));
		*cfd = accept(fd, (struct sockaddr *) &client_addr, &client_len);
		pthread_create(&tid, NULL, main_thread, cfd);
		printf("Transaction completed!\n");
	}
	
	close(fd);
	cache_destroy();
	pthread_mutex_destroy(&mutex);
	return 0;
}

// The main thread function that sets up the thread. varg is of type conn_info
void *main_thread(void *varg) {
	int cfd = *((int *) varg);
	// Preventing memory leaks
	pthread_detach(pthread_self());
	free(varg);
	// Parse and forward the requests
	parse(cfd);
	close(cfd);
	return NULL;
}

// Parse the request from cfd the client's fd
void parse(int cfd) {
	printf("\n");
	// Precondition
	if(cfd < 0) {
		perror("error accepting connection from client\n");
		return;
	}

	rio_t rp;
	// Handle requests from client
	int sfd;
	char nohttp_url[MAXLINE];
	rio_readinitb(&rp, cfd);
	if((sfd = foward_request(&rp, nohttp_url)) < 0)
		return;
	
	// Handle response from server
	rio_readinitb(&rp, sfd);
	if(foward_response(&rp, cfd, nohttp_url) < 0)
		return;

	close(sfd);
	return;
}

// Foward the request data from client's rio_t to the server (sfd), which is returned
// Additionally it updates nohttp_url with the proper url
int foward_request(rio_t *rp, char *nohttp_url) {
	char uri[MAXLINE], req_type[MAXLINE], http[MAXLINE], leftover[MAXLINE], nohttp[MAXLINE], line[MAXLINE], path[MAXLINE],
		port[MAXLINE], host[MAXLINE], request[MAX_OBJECT_SIZE];

	if(rio_readlineb(rp, line, MAXLINE) < 0){
		perror("error reading request line in parse_request");
		return -1;
	}

	// Parse the request
	sscanf(line, "%s %s %s", req_type, uri, http);

	if(strcmp(req_type, "GET") != 0) {
		perror("only GET is supported\n");
		return -1;
	}

	// Loop only executes once, it eliminates need for GOTO. 
	// localhost case
	if(uri[0] == '/') {
		// Set default port, might be changed later
		strcpy(port, "80");
		strcpy(path, uri);
	}
	
	else {
		if(sscanf(uri, "http://%s", nohttp) != 1)
			sscanf(uri, "%s", nohttp);
		sscanf(nohttp, "%[^/]%s", leftover, path);
		if(sscanf(leftover, "%[^:]:%s", host, port) == 1) {
			port[0] = '8';
			port[1] = '0';
		}
	}

	// Localhost pre-read check
	if(strstr(leftover, "localhost") != 0) {
		char tmp[MAXLINE] = "localhost";
		strcat(tmp, path);
		strcpy(nohttp_url, tmp);
	}

	else
		strcpy(nohttp_url, leftover);
	
	// Erase the request string
	strcpy(request, "");
	// Save request line
	sprintf(request, "%s %s HTTP/1.0\r\n", req_type, path);

	// Host is not defined, so read headers to obtain the host.
	// Read request headers
	int flag = 0;
	while(strcmp(line, "\r\n")) {
		if(rio_readlineb(rp, line, MAXLINE) < 0) {
			perror("error reading a request line from client");
			return -1;
		}
		if(!strcmp(line, "\r\n"))
			break;
		// If the request contains a custom host, then set our host flag
		if(strstr(line, "Host:")) {
			if(strstr(line, "Host: localhost")) {
				sscanf(line, "Host: localhost:%s", port);
				strcpy(host, "localhost");
			}
			
			flag = 1;
		}
		// If the line is not a custom header
		if(!strstr(line, "User-Agent:") && !strstr(line, "Proxy-Connection:") && !strstr(line, "Connection:"))
			strcat(request, line);
	}

	// See if request is cached
	int i = cache_find(nohttp_url);
	// Request is already cached, so return and write
	if(i >= 0) {
		char *buf = list[i]->content;
		if(rio_writen(rp->rio_fd, buf, list[i]->content_size) < 0) {
			perror("error sending cached contents to client");
			return -1;
		}
		printf("Successfully sent cached contents to client!\n");
		return 0;
	}

	// Client did not send a custom host, so use default
	if(!flag) {
		sprintf(line, "Host: %s\r\n", host);
		strcat(request, line);
	}

	// Add the custom headers
	strcat(request, user_agent_hdr);
	strcat(request, prox_conn);
	strcat(request, conn);
	strcat(request, "\r\n");

	// Open the server connection
	int sfd = open_clientfd(host, port);
	if(sfd < 0) {
		perror("error opening up connection to server\n");
		return -1;
	}

	printf("server connected\n");

	// Forward the request to the server
	if(rio_writen(sfd, request, strlen(request)) < 0){
		perror("error sending the header to server");
		return -1;
	}
	printf("Request forwarded\n");
	return sfd;
}

// Foward the response data from rp (a server) to cfd (the client)
int foward_response(rio_t *rp, int cfd, char *nohttp_url) {
	char line[MAXLINE], response[MAXLINE], content_line[MAX_OBJECT_SIZE], content[MAX_OBJECT_SIZE * 10];

	strcpy(response, "");

	// Recieve response header from server
	int line_size = 0, response_size = 0, content_size = 0;
	while((line_size = rio_readlineb(rp, line, MAXLINE)) != 0) {
		strcat(response, line);
		response_size += line_size;

		// Additional loop guard, after the line was added
		if(!strcmp(line, "\r\n"))
			break;
	}

	// Write reponse header to client
	if(rio_writen(cfd, response, response_size) < 0) {
		perror("error writing response header to client");
		return -1;
	}

	strcpy(content, "");

	// Receive content from server
	// Need to use a different method of obtaining the content because it may be binary
	while((line_size = rio_readnb(rp, content_line, MAX_OBJECT_SIZE)) != 0) {
		memcpy(content + content_size, content_line, line_size);
		content_size += line_size;
	}

	// Write reponse content to client
	if(rio_writen(cfd, content, content_size) < 0) {
		perror("error writing response content to client\n");
		return -1;
	}

	printf("Response forwarded\n");
	printf("Caching data...\n");
	cache_add(content, content_size, nohttp_url);
	printf("Caching completed!\n");

	return 0;
}