/* 
 * proxy.c - CS:APP Web proxy
 *
 */ 

#include "csapp.h"

int 	request_cnt;
int 	listenfd;

struct threadpool_task {
	struct 	sockaddr_in *clientaddr;
	int *connfd;
};

typedef struct threadpool_task *tt;

struct threadpool {
	pthread_mutex_t mutex;
	pthread_cond_t  cond_empty; /* cond variable to wait on empty buffer */
	pthread_cond_t  cond_full;  /* cond variable to wait on full buffer */
	pthread_t 	*threads;
	tt 	 	task_queue;
	int 		num_thread;	/* number of threads */
	int 		queue_size;	/* size of the task queue */
	int 		head;		/* index of the first element */
	int 		tail;		/* index of the last element */
	int 		num_started;	/* number of started threads  */
	int 		num_pend;	/* number of pending tasks in queue */
};
typedef struct threadpool *tp;

/*
 * Function prototypes
 */
void    format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
            char *uri, int size);
int     parse_uri(char *uri, char *target_addr, char *path, int *port);
void    create_log(char *logstring, char *logfn);
ssize_t Rio_readlineb_w(rio_t *r, void *usrbuf, size_t n);
ssize_t Rio_readnb_w(rio_t *r, void *ptr, size_t nbytes);
void    Rio_writen_w(int fd, void *usrbuf, size_t n);
int     open_clientfd_ts(char *hostname, int port);
void *	threadpool_thread(void *vargp);
tp 		init_threadpool(int num_thread, int queue_size);
int     free_threadpool(tp pool);
int 	threadpool_add(tp pool, struct 	sockaddr_in *clientaddr, int *connfd);

/* flag for debugging */
int debug_flag = 0;

/* 
 *
 * Request:
 *    num_thread: Number of worker threads
 *    queue_size: size of the task queue
 *  
 * Effect:
 *    initialize a new thread pool or null if error happened
 */
tp
init_threadpool(int num_thread, int queue_size)
{
	/* check for invalid input. */
	if (num_thread <= 0)
		return NULL;
	tp pool;
	int count;

	if ((pool = (tp)Malloc(sizeof(struct threadpool))) == NULL) {
		if (pool) 
			free_threadpool(pool);
		return NULL;	
	}
	/* initialize */
	pool->num_thread = 0; 
	pool->queue_size = queue_size;
	pool->head = pool->tail = pool->num_started = pool->num_pend = 0;

	/* allocate threads */
	pool->threads = (pthread_t *)Malloc(sizeof(pthread_t) * num_thread);
	pool->task_queue = (tt)Malloc(sizeof(struct threadpool_task) * 
				      queue_size);
	
	/* initialize mutex and conditional variable */
	Pthread_mutex_init(&(pool->mutex), NULL);
	Pthread_cond_init(&(pool->cond_empty), NULL);
	Pthread_cond_init(&(pool->cond_full), NULL);

	if ((pool->threads == NULL) || (pool->task_queue == NULL)) {
		if (pool) 
			free_threadpool(pool);
		return NULL;
	}
	/* start worker threads */
	for (count = 0; count < num_thread; count++) {
		Pthread_create(&(pool->threads[count]), NULL, 
		    threadpool_thread, (void *)pool); 
		pool->num_thread++;
		pool->num_started++; 
	}
	return pool;
}	

/*
 * Request:
 *    an initialized thread pool
 *  
 * Effect:
 *    free the memeory resources used by the thread pool
 */
int 
free_threadpool(tp pool) 
{
	if (debug_flag) {
		fprintf(stdout, "reached_free_threadpool\n");
		fflush(stdout);
	}
	if ((pool == NULL) || (pool->num_started > 0))
		return -1;
	if (pool->threads) {
		Free(pool->threads); 
		Free(pool->task_queue);
		Pthread_mutex_destroy(&(pool->mutex));
        Pthread_cond_destroy(&(pool->cond_empty));
       	Pthread_cond_destroy(&(pool->cond_full));
	}
	Free(pool);
	return 0;
}

/* 
 * Request:
 *    an initialized thread pool
 *    a ptr to struct request_info
 *    a ptr to connected file descriptor
 * Effect:
 *    add a pending task to the task_queue
 */
int 
threadpool_add(tp pool, struct sockaddr_in *clientaddr, int *connfd)
{	
	if (debug_flag) {
		fprintf(stdout, "reached_threadpool_add\n");
		fflush(stdout);
	}

	int next;
	/* thread pool is not valid */
	if (pool == NULL) {
		fprintf(stderr, "ERROR: threadpool_invalid."); 
		exit(1);
	}

	next = pool->tail + 1;
	/* round next back to 0 if next reaches queue size */
	next = (next == pool->queue_size) ? 0 : next; 

	/* add task to queue */
	pool->task_queue[pool->tail].clientaddr = clientaddr;
	pool->task_queue[pool->tail].connfd = connfd;

	pool->tail = next;
	pool->num_pend += 1;

	return 0;
}	

/* 
 * thread routine 
 *
 * Request:
 * 	pointer to struct threadpool
 * Effect:
 *  	concurrent web proxy that log requests
 *	i.e. use threads to deal with multiple clients concurrently
 */
void *
threadpool_thread(void *vargp) 
{
	if (debug_flag){
		fprintf(stdout, "threadpool_thread_reached\n");
		fflush(stdout);
	}

	tp pool = (tp) vargp;
	struct sockaddr_in clientaddr;
	int portnum;
	char hostname[MAXLINE];
	char uri[MAXLINE];
	char request[MAXLINE];
	int rsize = 0;

	int connfd;
	/* threads run as detached to avoid memory leak */
	Pthread_detach(pthread_self());

	while(1) {
		/* Acquire mutex lock */
		Pthread_mutex_lock(&(pool->mutex));

		/* If buffer is empty, wait until something is added. */
		while (pool->num_pend == 0)
			Pthread_cond_wait(&(pool->cond_empty), &(pool->mutex));

		if (pool->num_pend == 0)
			break;

		/* take one task from the task queue */
		clientaddr = *(pool->task_queue[pool->head].clientaddr);
		Free(pool->task_queue[pool->head].clientaddr);

		connfd = *(pool->task_queue[pool->head].connfd);
		Free(pool->task_queue[pool->head].connfd);

		pool->head += 1;
		pool->head = (pool->head == pool->queue_size) ? 0 : pool->head;

		/* Signal producer thread */
		Pthread_cond_signal(&(pool->cond_full));

		pool->num_pend -= 1;

		/* Release mutex lock */
		Pthread_mutex_unlock(&(pool->mutex));

		/* do task */
		/* make connection with server and send request to server */
		/* read and echo text lines until EOF is encountered */
		rio_t rio;
		rio_t rio_s;
		int clientfd;
		size_t n;
		char buf[MAXLINE], method[MAXLINE], version[MAXLINE];
		char *pathname = Malloc(MAXLINE);
		/* associates the descriptor connfd with a read buffer 
		 * at address rio */
		Rio_readinitb(&rio, connfd);
		/* Need to modify the first line, change uri to pathname */
		Rio_readlineb_w(&rio, buf, MAXLINE);

		/* pass method, uri, version info from request line */
		sscanf(buf, "%s %s %s", method, uri, version);
		/* Read HTTP request */
		if (!strstr(buf, "GET")) {
			fprintf(stdout, 
			    "Received non-GET request; this method is not implemented.\n");
			/* Clean up before leaving */
			Free(pathname);
			close(connfd);
			continue;
		}
		/* Parse the request and extract filename, server name info */
		if (parse_uri(&buf[4], hostname, pathname, 
				&portnum) < 0) {
			fprintf(stderr, "Error parsing URI\n");
			continue;
		}

		/* first open connection to end server and forward request */
		/* client establishes connection with a server running on host 
		 * hostname and listen for connection requests on the port. */
		if ((clientfd = open_clientfd_ts(hostname, portnum)) < 0) {
			Free(pathname);
			close(connfd);
			continue;
		}
		
		Rio_readinitb(&rio_s, clientfd);
		/* Write into the first request line */
		sprintf(request, "%s %s %s\n", method, pathname, version);

		strcat(request, "Connection: close\n");

		Rio_writen_w(clientfd, request, strlen(request));

		fprintf(stdout, "Request %d: Received request %sFrom %s\n", 
			request_cnt++, request, 
				inet_ntoa(clientaddr.sin_addr));
		Free(pathname);
		
		/* Now keep processing the rest headers of the request */
		while ((n = Rio_readlineb_w(&rio, buf, MAXLINE)) != 0) {
			/* Refuse usage of persistent connections */
			if (strstr(buf, "Connection: ")) {
				fprintf(stdout, 
					"Stripping out Connection header\n");
				continue;
			} 
			if (strstr(buf, "Proxy-Connection: ")) {
				fprintf(stdout, 
				    "Stripping out Proxy-Connection header\n");
				continue;
			}
			Rio_writen_w(clientfd, buf, strlen(buf));

			fprintf(stdout, 
				"Request %d: Received request %sFrom %s\n", 
			   request_cnt++, buf, inet_ntoa(clientaddr.sin_addr));
			/* Jump out when request headers end */
			if (buf[0] == 13)
				break;
		}

		fprintf(stdout, "************End of Request***********\n\n");

		/* Send request to end server, receive the reply from server 
		 * and forward the reply to the browser, 
		 * provide info to sockaddr structure. */ 
		char response[MAXLINE], logstring[MAXLINE];
		/* Read the response header lines */
		while ((n = Rio_readlineb_w(&rio_s, response, MAXLINE)) != 0) {
			fprintf(stdout, "%s", response);
			rsize += n;
			Rio_writen_w(connfd, response, n);
	       		/* Jump out when response headers terminates with 
	       		 * empty line */
			if (response[0] == 13) {
				rsize += n;
				break;
			}
		}
		/* receive the reply and forward to browser */
		while ((n = Rio_readnb_w(&rio_s, response, MAXLINE)) != 0) {
			fprintf(stdout, 
			    "Proxy forwarded %d bytes to browser.\n", (int)n);
			rsize += n;
			if (rio_writen(connfd, response, n) != (int) n) {
				fprintf(stdout, "Fail to write into file descriptor\n");
				close(connfd);
				break;
			}
		} 

		close(clientfd);
		/* Write the connection req_struct into log file */
		format_log_entry(logstring, &clientaddr, uri, rsize);
		/* Acquire mutex lock. */
		Pthread_mutex_lock(&(pool->mutex));
		/* update log file */
		create_log(logstring, "proxy.log");
		/* Release mutex lock */
		Pthread_mutex_unlock(&(pool->mutex));

		fprintf(stdout, 
		     "Proxy received %d bytes in total from server%s", rsize,
			"\n***********End of Response**********\n\n");
		/* Close file descriptors */
		fprintf(stdout, 
			"*********Finished One Transaction********\n\n");
		
		close(connfd);
	}
	pool->num_started -= 1;

	Pthread_mutex_unlock(&(pool->mutex));

	return NULL;
}

/* 
 * main - Main routine for the proxy program 
 */
int
main(int argc, char **argv)
{
	if (debug_flag){
		fprintf(stdout, "main_reached\n");
		fflush(stdout);
	}

	/* Check the arguments. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}
	/* Ignore broken pipe error */
	Signal(SIGPIPE, SIG_IGN);
 
	int port = atoi(argv[1]);
	request_cnt = 0;
	/* Open listen file descriptor one time */
	/* this socket will accept connection (connect) requests from clients */
	listenfd = Open_listenfd(port); 

	tp pool;
	/* initialize thread pool with 16 worker threads and 
	 * task_queue size 512 */
	pool = init_threadpool(16, 64);

	while (1) {
		/* Initialize data structures to store connection info */ 
		int *connfd = Malloc(sizeof(int));
		/* Receive request from browser */
		/* client address */
		struct sockaddr_in *clientaddr = 
		    (struct sockaddr_in *)Malloc(sizeof(struct sockaddr_in));

		socklen_t clientlen = sizeof(struct sockaddr_in);

		/* accept returns a connected descriptor (connfd, used to  
	 	 * communiated with the client) with the same properties as the  
	 	 * listening descriptor (listenfd) */
		/* waits for a connection request from a client to arrive on  
	 	 * the listening descriptor listenfd, then fills in the 
	 	 * client’s socket address in addr, and returns a connected  
	 	 * descriptor that can be used to communicate with the client 
	 	 * using Unix I/O functions */
		*connfd = Accept(listenfd, (SA *) clientaddr, &clientlen);
		/* client and server can pass data back and forth by reading  
	 	 * clientfd and writting connfd */

		/* Acquire mutex lock */
		Pthread_mutex_lock(&(pool->mutex));

		/* If buffer is full, wait until signaled. */
		if (pool->num_pend == pool->queue_size) {
			Pthread_cond_wait(&(pool->cond_full), &(pool->mutex));
		}
		/* add a pending task to the task_queue. */
		threadpool_add(pool, clientaddr, connfd);

		/* Signal consumer threads. */
		Pthread_cond_signal(&(pool->cond_empty));
 
		request_cnt++;
		/* Release mutex lock */
		Pthread_mutex_unlock(&(pool->mutex));
	}
	
	if (pool) 
		free_threadpool(pool);
	if (close(listenfd) < 0) {
		fprintf(stdout,"Error closing listenfd\n");
		exit(1); 
	}
	/* Return success. */
	return (0);
}

/*
 * create_log - Create or modify log entries
 * Request:
 *   the logstring to be entered, and name of log file
 * Effect:
 *   Keep track of all requests in the given log file
 */
void
create_log(char *logstring, char *fn)
{
	/* mode ab+: append; open or create binary file for update, writing at
	 * end-of-file; i.e. read and write */
	FILE *fp = Fopen(fn, "ab+");
	fprintf(fp, "%s\n", logstring);
	Fclose(fp);
}

/* 
 *
 * Request:
 *    Read buffer of type rio_t at address rio
 *    Memory location usrbuf
 *    maxlen bytes
 *  
 * Effect:
 *    Reads the next text line from file r (including the terminating newline 
 *    character), copies it to memory location usrbuf, and terminates the text 
 *    line with the null (zero) character.
 *    Reads at most maxlen-1 bytes, one byte for the terminating null character.
 */
ssize_t 
Rio_readlineb_w(rio_t *r, void *usrbuf, size_t maxlen)
{
	ssize_t sz;
	if ((sz = rio_readlineb(r, usrbuf, maxlen)) < 0) {
		fprintf(stdout, "Failed to read line: %s\n", strerror(errno));
		return 0;
	}
    return sz;
}

/* 
 *
 * Request:
 *    Read buffer of type rio_t at address rio
 *    memory location ptr
 *    nbytes bytes
 *  
 * Effect:
 *    transfers up to nbytes from file r to memory location ptr
 */
ssize_t
Rio_readnb_w(rio_t *r, void *ptr, size_t nbytes)
{
	ssize_t n;
	if ((n = rio_readnb(r, ptr, nbytes)) < 0) {
		fprintf(stdout, "Failed to read response: %s\n", strerror(errno));
		return 0;
	}
	return n;
}

/* 
 *
 * Request:
 *    descriptor fd
 *    memory location ptr
 *    nbytes bytes
 *  
 * Effect:
 *    transfers n bytes from location usrbuf to descriptor fd
 */
void 
Rio_writen_w(int fd, void *usrbuf, size_t n)
{
	if (rio_writen(fd, usrbuf, n) != (int) n)
		fprintf(stdout, "Failed to write into file descriptor\n");
}

/*
 * open_clientfd_ts - open connection to server at <hostname, port> 
 *   and return a socket descriptor ready for reading and writing.
 *   Returns -1 and sets errno on Unix error. 
 *   Returns -2 and sets h_errno on DNS (gethostbyname) error.
 *   Thread Safe version of open_clientfd
 */ 
int open_clientfd_ts(char *hostname, int port) 
{
    	int clientfd, error;
        struct addrinfo *ai;
        struct sockaddr_in serveraddr; 

        /* Set clientfd to a newly created stream socket */
        if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)       
        	return -1;
        /* Use getaddrinfo to get the server's IP */
        if ((error = getaddrinfo(hostname, NULL, NULL, &ai)) != 0) {
        	fprintf(stderr, "ERROR: %s\n", gai_strerror(error)); 
        	return -1; 
        } 
        /* Set the address of serveraddr to be server's IP address and port.
         * Ensure that the IP address and port are in network byte order. */
        bzero((char *) &serveraddr, sizeof(serveraddr));
        serveraddr.sin_family = AF_INET;
        bcopy(ai->ai_addr, (struct sockaddr *)&serveraddr, ai->ai_addrlen);
        serveraddr.sin_port = htons(port);

        /* Establish a connection with the server with connect */
        if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
                return -1;
        freeaddrinfo(ai);
        return clientfd;
} 

/*
 * parse_uri - URI parser
 * 
 * Requires: 
 *   The memory for hostname and pathname must already be allocated
 *   and should be at least MAXLINE bytes.  Port must point to a
 *   single integer that has already been allocated.
 *
 * Effects:
 *   Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 *   the host name, path name, and port. Return -1 if there are any
 *   problems and 0 otherwise.
 */
int 
parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
	int len, i, j;
	char *hostbegin;
	char *hostend;
	
	if (strncasecmp(uri, "http://", 7) != 0) {
		hostname[0] = '\0';
		return (-1);
	}

	/* Extract the host name. */
	hostbegin = uri + 7;
	hostend = strpbrk(hostbegin, " :/\r\n");
	if (hostend == NULL)
		hostend = hostbegin + strlen(hostbegin);
	len = hostend - hostbegin;
	strncpy(hostname, hostbegin, len);
	hostname[len] = '\0';

	/* Look for a port number.  If none is found, use port 80. */
	*port = 80;
	if (*hostend == ':')
		*port = atoi(hostend + 1);
	
	/* Extract the path. */
	for (i = 0; hostbegin[i] != '/'; i++) {
		if (hostbegin[i] == ' ') 
			break;
	}

	if (hostbegin[i] == ' ')
		strcpy(pathname, "/");
	else {
		for (j = 0; hostbegin[i] != ' '; j++, i++) {
			pathname[j] = hostbegin[i];
		}
		pathname[j] = '\0';
	}

	return (0);
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 *
 * Requires:
 *   The memory for logstring must already be allocated and should be
 *   at least MAXLINE bytes.  Sockaddr must point to an allocated
 *   sockaddr_in structure.  Uri must point to a properly terminated
 *   string.
 *
 * Effects:
 *   A properly formatted log entry is stored in logstring using the
 *   socket address of the requesting client (sockaddr), the URI from
 *   the request (uri), and the size in bytes of the response from the
 *   server (size).
 */
void
format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri,
    int size)
{
	time_t now;
	unsigned long host;
	unsigned char a, b, c, d;
	char time_str[MAXLINE];

	/* Get a formatted time string. */
	now = time(NULL);
	strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z",
	    localtime(&now));

	/*
	 * Convert the IP address in network byte order to dotted decimal
	 * form.  Note that we could have used inet_ntoa, but chose not to
	 * because inet_ntoa is a Class 3 thread unsafe function that
	 * returns a pointer to a static variable (Ch 13, CS:APP).
	 */
	host = ntohl(sockaddr->sin_addr.s_addr);
	a = host >> 24;
	b = (host >> 16) & 0xff;
	c = (host >> 8) & 0xff;
	d = host & 0xff;

	/* Return the formatted log entry string */
	sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri,
	    size);
}

/*
 * The last lines of this file configure the behavior of the "Tab" key in
 * emacs.  Emacs has a rudimentary understanding of C syntax and style.  In
 * particular, depressing the "Tab" key once at the start of a new line will
 * insert as many tabs and/or spaces as are needed for proper indentation.
 */

/* Local Variables: */
/* mode: c */
/* c-default-style: "bsd" */
/* c-basic-offset: 8 */
/* c-continued-statement-offset: 4 */
/* indent-tabs-mode: t */
/* End: */
