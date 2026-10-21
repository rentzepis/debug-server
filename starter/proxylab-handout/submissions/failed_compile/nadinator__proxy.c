/*
 * 					  CMUQ
 * 			     15-213, Fall '20
 * 				    Proxy Lab
 *
 *			Written by Nadim Bou Alwan
 * 			   Andrew ID: nboualwa 
 *
 *
 *
 * proxy.c
 * CODE DESCRIPTION
 *
 * A concurrent (threaded) proxy that handles simple requests 
 * and implements caching of requests.
 *
 * Borrows much code from CS:APP3e's tiny.c.
*/

#include <stdio.h>
#include "csapp.h"
#include "webcache.h"

/* Network-compatible rio macros */
#define RIOWRITEN(fd, buf, n)    {if (my_rio_writen(fd, buf, n) <= 0) return;}
#define RIOREADLINEB(fd, buf, n) {if (my_rio_readlineb(fd, buf, n) < 0) return -1;}

/* You automatically gain 100 points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* Shared web cache */
cache_t *cache;

/* Client-handling functions */
void *thread(void *fd); 
void process_client_request(int fd);
/* Parsing functions */
int parse_uri(char *uri, char *host, char *path, char *port);
int parse_req_headers(rio_t *rp, char *extra_headers, char *hdr_host);
int parse_req_line(rio_t *rp, int cp_fd, char *host, char *path, char *port);
/* Error-handling functions */
void clienterror(int fd, char *cause, char *errnum, 
		 		 char *shortmsg, char *longmsg);
/* Self-defined RI/O wrappers */
ssize_t my_rio_writen(int fd, void *usrbuf, size_t n);
ssize_t my_rio_readnb(rio_t *rp, void *usrbuf, size_t n);
ssize_t my_rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen);
/* Misc functions */
void memset_str(char *s);


/*
 * main - main proxy routine. Reads input port number and opens 
 *		listening connection on it, then handles request/response
 *		transaction.
 */
int main(int argc, char **argv)
{
	int *cp_fd;	  // Client/proxy connection fd
    int listenfd; // Proxy listening fd
    pthread_t tid;
    char *listen_port;
    socklen_t clientlen;
    struct sockaddr_in clientaddr;

    /* Ignore SIGPIPE signals */
    Signal(SIGPIPE, SIG_IGN);

    /* Initialize cache */
    cache = cache_init();

    /* Check command line args */
    if (argc != 2) 
    {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }
    listen_port = argv[1];

    /* Check if listening port opened */
    if ((listenfd = Open_listenfd(listen_port)) < 0)
    	exit(1);

    /* Main server loop */
    while (1) 
    {
		clientlen = sizeof(clientaddr);
		cp_fd = Malloc(sizeof(int));
		*cp_fd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
		/* Create a new thread to deal with client request */
		Pthread_create(&tid, NULL, thread, cp_fd);
    }

    /* Free cache elements when done */
    free_cache(cache);

    return 0;
}


/*********************************/
/*** CLIENT-HANDLING FUNCTIONS ***/
/*********************************/

/*
 * thread - thread function that handles client requests
 */
void *thread(void *fd)
{	
	/* Save the passed fd value and free it */
	int cp_fd = *((int *)fd);
	Free(fd);
	/* Run in detached mode */
	Pthread_detach(Pthread_self()); 
	/* Process client's request */
	process_client_request(cp_fd);
	/* Close connection when done */
	Close(cp_fd);
	/* Return NULL, for C works in mysterious ways */
	return NULL;
}

/*
 * process_client_request - The heart of the proxy. Check the client request
 * 		 for errors and forward it to server if none are found. 
 * 		 Return early upon error.
 */
void process_client_request(int cp_fd) 
{
	rio_t rio;
    int ps_fd; 			// Proxy/server fd 
    line_t *line; 		// Cache line containing web object
    char buf[MAXLINE];  // Reding buffer
    char extra_headers[MAXLINE], hdr_host[MAXLINE];
    char host[MAXLINE], path[MAXLINE], port[MAXLINE];  
    
    /* Reset all used strings */
    memset(buf, 0, MAXLINE-1);
    memset(host, 0, MAXLINE-1);
    memset(path, 0, MAXLINE-1);
    memset(port, 0, MAXLINE-1);
    memset(hdr_host, 0, MAXLINE-1);
    memset(extra_headers, 0, MAXLINE-1);

    /* Parse request line and fill in passed pointers on success */
    if(parse_req_line(&rio, cp_fd, host, path, port) < 0)
    	return;

    /* Parse request headers and check for non-default headers */
    if(parse_req_headers(&rio, extra_headers, hdr_host) < 0)
    	return;

    /*** Building request to be forwarded to server ***/
    /* Write request line */
    sprintf(buf, "GET %s HTTP/1.0\r\n", path);
    /* Write request headers */
    /* Check if Host header was specified in client request */
    if (hdr_host == NULL)
    	/* Write header-specified host */
    	sprintf(buf, "%sHost: %s\r\n", buf, hdr_host);
    else
    	/* Use host provided in URI otherwise */
    	sprintf(buf, "%sHost: %s\r\n", buf, host);
    /* Write rest of default headers */
    sprintf(buf, "%s%s", buf, user_agent_hdr);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sProxy-connection: close\r\n", buf);
    /* Forward additional headers, if any */
    if (extra_headers == NULL)
    	sprintf(buf, "%s\r\n", buf);
   	else
   		sprintf(buf, "%s%s", buf, extra_headers);

   	/* Check the cache for request;
   	 * Returns the cache line if found, otherwise NULL 
   	 */
   	line = in_cache(cache, buf); //// CACHE READ ////
   	/* Write back to client directly if cache hit */
   	if (line) {
   		RIOWRITEN(cp_fd, line->web_obj, line->size);
   	}
   	/* Otherwise connect to server and forward the request */
   	else {
   		/* Connect server to proxy */
    	if ((ps_fd = Open_clientfd(host, port)) < 0) {
    		clienterror(cp_fd, "request_line", "400", "Bad request",
                	"Proxy could not understand the request");
    		return;
    	}
    	/* Initialize cache variables */
   		char req[MAXLINE]; 	// Client request and web object
   		size_t s = 0;		// Size of web object
   		ssize_t nread;		// Bytes read from server
   		char web_obj[MAX_OBJECT_SIZE]; // Web object received from server
   		/* Initialize rio to proxy/server connection */
   		Rio_readinitb(&rio, ps_fd);
   		/* Save the built request */ 
   		strcpy(req, buf);
   		/* Send the built request to server */
   		RIOWRITEN(ps_fd, buf, strlen(buf)); 
    	/* Read server response and write to client */
		while((nread = my_rio_readnb(&rio, buf, MAXLINE)) != 0)
		{	
			/* Write back to client */
			RIOWRITEN(cp_fd, buf, nread); 
			/* Update web object for caching */
			memcpy(web_obj+s, buf, nread);
			/* Update web object size */
			s += nread;
		}
		/* Add the web object to the cache */
		add_object(cache, req, web_obj, s); //// CACHE WRITE ////
	}
}

/*************************************/
/*** END CLIENT-HANDLING FUNCTIONS ***/
/*************************************/


/*************************/
/*** PARSING FUNCTIONS ***/
/*************************/

/*
 * parse_req_headers - read and parse HTTP request headers, ignoring values
 * 				given for Host, User-Agent, Connection, and Proxy-connection. 
 *				Returns 0 on success, -1 on error.
 */
int parse_req_headers(rio_t *rp, char *extra_headers, char *hdr_host) 
{
    char buf[MAXLINE];

    /* Reset all used strings */
    memset_str(buf);

    /* Read the first request header */
    RIOREADLINEB(rp, buf, MAXLINE);
    /* Read the rest, if any */
    while(strcmp(buf, "\r\n")) {
    	/* Check for "Host:" header */
    	if (strstr(buf, "Host:"))
    	{	
    		/* Skip "Host: " and "\r\n" and copy into host */
    		strncpy(hdr_host, buf+6, strlen(buf+6)-2);
    		/* Add a null terminator */
    		strcat(hdr_host, "");
    	}
    	/* Ignore default headers */
    	else if (!strstr(buf, "Connection:") && 
    			!strstr(buf, "Proxy-connection:") && 
    			!strstr(buf, "User-Agent:"))
    	{
    		/* Add any other extra headers */
    		strcat(extra_headers, buf);
    	}
    	/* Read next header */
    	RIOREADLINEB(rp, buf, MAXLINE);
    }

    /* Terminate the header string according to RFC 1945 specs */
    strcat(extra_headers, "\r\n");

    return 0;
}

/*
 * parse_req_line - read and parse HTTP request line for host, path, and port,
 *				while making checks for client errors
 */
int parse_req_line(rio_t *rp, int cp_fd, char *host, char *path, char *port)
{
    char buf[MAXLINE]; 
    char method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    /* Reset all used strings */
    memset_str(buf);
    memset_str(uri);
    memset_str(version);
    memset_str(method);

    /* Read request line */
    Rio_readinitb(rp, cp_fd);
    /* Fill buffer with first request header */
    RIOREADLINEB(rp, buf, MAXLINE);

    /* Check that request line contains three strings (method URI version) */
    if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
        clienterror(cp_fd, "request_line", "400", "Bad request",
                "Proxy requires: method URI version");
        return -1;
    }
    
    /* Check that version is "HTTP/1.0" or "HTTP/1.1" */
    if (strcasecmp(version, "HTTP/1.0") && strcasecmp(version, "HTTP/1.1")) { 
        clienterror(cp_fd, version, "501", "Not implemented",
                "Proxy does not implement this version");
        return -1;
    }
    /* Forwarded version is always HTTP/1.0 */
    strcpy(version, "HTTP/1.0");
  	
  	/* Check that method is "GET" */
    if (strcasecmp(method, "GET")) { 
        clienterror(cp_fd, method, "501", "Not Implemented",
                "Proxy does not implement this method");
        return -1;
    }

    /* Update host, path, and port values */
    if (parse_uri(uri, host, path, port) < 0)
    {
    	clienterror(cp_fd, "request_line", "400", "Bad request",
                "Proxy could not understand the request");
        return -1;
    }

    return 0;
}

/*
 * parse_uri - parse URI into host, path, and port arguments
 */
int parse_uri(char *uri, char *host, char *path, char *port)
{
	char def_path[MAXLINE] = "/";	// Default path
    char def_port[MAXLINE] = "80";	// Default port
    char *buf, *path_start, *port_start;

    /* Check that "http://" included in URI */
    if (!(strstr(uri, "http://")))
    	return -1;

    /* Skip "http://" */
    buf = uri + 7;

    /* Check if port and/or path specified */
    path_start = strpbrk(buf, "/");
    port_start = strpbrk(buf, ":");

    /* Path and port not specified, so use default values */
    if ((!path_start) && (!port_start)) {
    	/* The uri itself is the host */
    	strcpy(host, buf);
    	strcpy(path, def_path);
    	strcpy(port, def_port);
    }
    /* Path not specified, but port is */
    else if (!path_start) {
    	strncpy(host, buf, port_start-buf);
    	strcpy(path, def_path);
    	strcpy(port, port_start+1);
    }
    /* Port not specified, but path is */
    else if (!port_start) {
    	strncpy(host, buf, path_start-buf);
    	strcpy(path, path_start);
    	strcpy(port, def_port);
    }
    /* Both path and port are specified */
    else {
    	strncpy(host, buf, port_start-buf);
    	strcpy(path, path_start);
    	strncpy(port, port_start+1, path_start-port_start-1);
    }
   
   	/* Reset strings */
   	memset_str(buf);
   	memset_str(path_start);
   	memset_str(port_start);

   	return 0;
}

/*****************************/
/*** END PARSING FUNCTIONS ***/
/*****************************/


/***********************/
/*** ERROR FUNCTIONS ***/
/***********************/

/*
 * clienterror - returns an error message to the client. 
 * 			Copy-pasted from tiny.c, except for rio_writen wrapper
 */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Reset all used strings */
    memset_str(buf);
    memset_str(body);

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Proxy Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Proxy Web Server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    RIOWRITEN(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    RIOWRITEN(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    RIOWRITEN(fd, buf, strlen(buf));
    RIOWRITEN(fd, body, strlen(body));
}

/***************************/
/*** END ERROR FUNCTIONS ***/
/***************************/


/*********************/
/*** RI/O WRAPPERS ***/
/*********************/

/*
 * my_rio_writen - network-compatible version of csapp.c's Rio_writen
 */
ssize_t my_rio_writen(int fd, void *usrbuf, size_t n) 
{
	if (rio_writen(fd, usrbuf, n)) 
	{
    	if (errno == EPIPE)
     		fprintf(stderr, "Proxy handled EPIPE\n");
    	else
    		fprintf(stderr, "rio_writen error\n");
  	}

  	return n;
}

/*
 * my_rio_readlineb - network-compatible version of csapp.c's Rio_readlineb
 */
ssize_t my_rio_readlineb(rio_t *rp, void *usrbuf, size_t maxlen) 
{
	ssize_t nread;

	if ((nread = rio_readlineb(rp, usrbuf, maxlen)) < 0) 
	{
  		if (errno == ECONNRESET)
    		fprintf(stderr, "Proxy handled ECONNRESET\n");
    	else
    		fprintf(stderr, "rio_readlineb error\n");
	}
  
  	return nread;
} 

/*
 * my_rio_readnb - network-compatible version of csapp.c's Rio_readnb
 */
ssize_t my_rio_readnb(rio_t *rp, void *usrbuf, size_t n) 
{
	ssize_t nread;

	if ((nread = rio_readnb(rp, usrbuf, n)) < 0) 
	{
  		if (errno == ECONNRESET)
    		fprintf(stderr, "Proxy handled ECONNRESET\n");
    	else
    		fprintf(stderr, "rio_readnb error\n");
	}
  
  	return nread;
} 

/*************************/
/*** END RI/O WRAPPERS ***/
/*************************/


/*********************/
/*** MISCELLANEOUS ***/
/*********************/

/*
 * memset_str - clear a string of its memory
 */
void memset_str(char *s)
{ 
	if (s != NULL) 
	{
		size_t n = sizeof(s);
		memset(s, 0, n+1); 
	}
}

/*************************/
/*** END MISCELLANEOUS ***/
/*************************/






