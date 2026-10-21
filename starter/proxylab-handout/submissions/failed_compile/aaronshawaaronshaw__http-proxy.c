/*
 * COMP 321 Project 6: Web Proxy
 *
 * This program implements a multithreaded HTTP proxy.
 *
 * Courtney Gardner --- cng3
 * Aaron Shaw --- aws6
 */ 

#include <assert.h>

#include "csapp.h"

static void	client_error(int fd, const char *cause, int err_num, 
		    const char *short_msg, const char *long_msg);
static char    *create_log_entry(const struct sockaddr_in *sockaddr,
		    const char *uri, int size);
static void handle_request(void *vargp);
static void *thread(void *vargp);
static int	parse_uri(const char *uri, char **hostnamep, char **portp,
		    char **pathnamep);

pthread_mutex_t threadlock;

struct thread_data {
	struct sockaddr_in clientaddr;
	char *hostname;
	char *port;
	int connfd;
	int id;
};

/*Global reference, shared variables */
static struct sockaddr_in *shared_buffer;
static struct sockaddr_in ref_ip[MAXLINE];

FILE *log_file;

/* 
 * Requires:
 *   argv must be a valid line read into the program and must contain 2
 *   elements. Argc must indicate the length of argv being 2.
 *
 * Effects:
 *   Given a a valid argc line, parses the request to determine what 
 *   connection is trying to be made. Establishes the proper connection,
 *   and generates a number of worker threads to complete the execution 
 *   of the various GET requests. It stores the sockaddresses in a
 *   global shared variable so that each thread can access its required
 *   information.
 */
int
main(int argc, char **argv)
{
	int listenfd, request_number;
	pthread_t tid;
	socklen_t clientlen;
	char hostname[MAXLINE], port[MAXLINE];
	struct sockaddr_in clientaddr;
	struct thread_data *request;


	request_number = 1;

	/* Check the arguments. */
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}

	/* Ignore broken pipe signals. */
	Signal(SIGPIPE, SIG_IGN);

	/* Open a file to log all requests. */
	log_file = Fopen("proxy.log", "a");

	/* Initialize a lock to handle concurrent requests. */
	Pthread_mutex_init(&threadlock, NULL);

	/* Listen for a connection request. */
	if ((listenfd = Open_listenfd(argv[1])) == -1) {
		client_error(listenfd, hostname, 404, "Not found",
			"Server could not be forwarded.");
	}

	/* Continuously listen for requests from the browser. */
	while (1) {
		clientlen = sizeof(struct sockaddr_in);

		/* Accept a request when one has been received. */
		request = Malloc(sizeof(struct thread_data));
		request->connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);

		/* Get the host and port information from the request. */
		getnameinfo((SA *) &clientaddr, clientlen, hostname, 
			MAXLINE, port, MAXLINE, 0);

		/* Store the request information for use by a given thread. */
		request->hostname = hostname;
		request->port = port;
		request->id = request_number;
		request->clientaddr = clientaddr;

		/* Lock the shared buffer while we put the request in. */
		pthread_mutex_lock(&threadlock);

		/* Ensure that our buffer has enough space for another request. */
		shared_buffer = 
			Malloc(sizeof(clientaddr) * (request_number) * MAXLINE);

		/* Add the new request to the buffer. */	
		shared_buffer[request_number] = clientaddr;
		ref_ip[0] = clientaddr;

		/* Create a new thread to handle the current request. */
		Pthread_create(&tid, NULL, thread, request);

		/* Increment the total requests by 1. */
		request_number++;

		/* Unlock once we're done accessing the shared buffer. */
		pthread_mutex_unlock(&threadlock);
	}

	/* Return success. */
	return (0);
}


/* 
 * Requires:
 *   vargp must be a valid thread_data struct.
 *
 * Effects:
 *   Geneartes a thread to complete the execution of work
 *   associated with the thread_data struct. Generates a request
 *   from vargp. Calls handle_request() to complete execution
 *   of the required work.
 */
static void
*thread(void *vargp)
{
	int connfd;
	struct thread_data *request;
	request = ((struct thread_data *) vargp);

	/* Get the connfd from the current request. */
	connfd = request->connfd;

	/* Destroy the thread when it terminates. */
	Pthread_detach(pthread_self());

	/* Free the request pointer from memory. */
	Free(vargp);

	/* Send the request information to the helper. */
	handle_request(request);

	/* Close the connection fd when we're finished with it. */
	Close(connfd);
	return NULL;
}

/* 
 * Requires:
 *   vargp must be a valid thread_data struct.
 *
 * Effects:
 *   Handles one HTTP transaction. Reads and parses the request line,
 *   and ignores all requests that are not proper GET methods. Reads 
 *   headers from the client to the server, stripping unsupportable 
 *   operations. Directly returns server information to the client.
 *   Logs all returned information
 */
static void
handle_request(void *vargp) {
	char *hostname, *port, *pathname, *entry;
	rio_t client_rio, server_rio;
	int connfd, serverfd, n, bytes_forwarded;
	struct thread_data *request;
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

	request = ((struct thread_data *) vargp);
	bytes_forwarded = 0;
	connfd = request->connfd;
	
	/* Associate client_rio with the connfd. */
	rio_readinitb(&client_rio, connfd);

	/* Read the first request header. */
	if ((n = rio_readlineb(&client_rio, buf, MAXLINE)) < 0) {
		client_error(connfd, buf, 400, "Bad request",
			"Request could not be understood by the server.");
		return;
	}

	/* Fill in the method, URI, and version info from the first header. */
	if (sscanf(buf, "%s %s %s", method, uri, version) != 3) {
		client_error(connfd, buf, 400, "Bad request",
			"Request could not be understood by the server.");
		return;
	}

	/* Check that the request is GET and return an error if not. */
	if (strcasecmp(method, "GET")) {
		client_error(connfd, method, 501, "Not implemented",
			"Server does not support the requested method.");
		return;
	}

	/* Parse the URI. */
	if (parse_uri(uri, &hostname, &port, &pathname) == -1) {
		client_error(connfd, uri, 400, "Bad request",
			"Request could not be understood by the server.");
		return;
	}

	/* Open a connection to the server. */
	if ((serverfd = Open_clientfd(hostname, port)) == -1) {
		client_error(connfd, hostname, 404, "Not found",
			"Server could not be found");
		return;
	}

	/* Associate server_rio with the serverfd descriptor. */
	rio_readinitb(&server_rio, serverfd);

	/* Send the request header to the server. */
	if ((rio_writen(serverfd, buf, strlen(buf))) < 0) {
		client_error(serverfd, buf, 400, "Bad request",
			"Request could not be understood by the server.");
		return;
	}

	/* Read all the header information. */
	while ((n = rio_readlineb(&client_rio, buf, MAXLINE)) > 0) {
		/* Stop looping once we've received all of the header info. */
		if (!strcmp(buf, "\r\n")) {
			break;
		/* Strip the Connection header. */	
		} else if (!strncmp(buf, "Connection", 10)) {
			printf("%s", buf);
			continue;
		/* Strip the Keep-Alive header. */	
		} else if (!strncmp(buf, "Keep-Alive", 10)) {
			printf("%s", buf);
			continue;
		/* Strip the Proxy-Connection header. */
		} else if (!strncmp(buf, "Proxy-Connection", 16)) {
			printf("%s", buf);
			continue;
		/* Otherwise, just send the header to the server. */	
		} else {
			printf("%s", buf);
			if ((rio_writen(serverfd, buf, n)) < 0) {
				client_error(serverfd, buf, 400, "Bad request",
					"Request could not be understood by the server.");
				return;
			}
		}
	}

	/* Write the close-connection header once everything has been read. */
	rio_writen(serverfd, "Connection: close\r\n", 19);
	rio_writen(serverfd, "\r\n", 2);

	/* Read the server's response. */
	while ((n = rio_readnb(&server_rio, buf, MAXLINE)) > 0) {
		/* Send the response to the broswer. */
		bytes_forwarded += n;
		if ((rio_writen(connfd, buf, n)) < 0) {
			client_error(serverfd, buf, 400, "Bad request",
				"Request could not be understood by the server.");
			return;
		}
	}

	/* Write the close-connection header once everything has been read. */
	printf("Connection: close\n");
	printf("\n");
	printf("*** End of Request ***\n");
	printf("Forwarded %d from end server to client\n", bytes_forwarded);

	/* Lock the buffer while you access the thread's logging info. */
	pthread_mutex_lock(&threadlock);

	if(request->id == 0) {
		shared_buffer[request->id] = ref_ip[0];
	}
	
	/* Get the log info using the clientaddr stored in our buffer. */
	entry = create_log_entry(&shared_buffer[request->id], 
		uri, bytes_forwarded);
	strcat(entry, "\n");
	
	/* Write the entry to the log file. */
	fwrite(entry, sizeof(char), strlen(entry), log_file);
	fflush(log_file);

	/* Unlock the buffer. */
	pthread_mutex_unlock(&threadlock);

	/* Free the memory for the entry. */
	Free(entry);

	/* Close the serverfd. */
	Close(serverfd);
}

/*
 * Requires:
 *   The parameter "uri" must point to a properly NUL-terminated string.
 *
 * Effects:
 *   Given a URI from an HTTP proxy GET request (i.e., a URL), extract the
 *   host name, port, and path name.  Create strings containing the host name,
 *   port, and path name, and return them through the parameters "hostnamep",
 *   "portp", "pathnamep", respectively.  (The caller must free the memory
 *   storing these strings.)  Return -1 if there are any problems and 0
 *   otherwise.
 */
static int
parse_uri(const char *uri, char **hostnamep, char **portp, char **pathnamep)
{
	const char *pathname_begin, *port_begin, *port_end;

	if (strncasecmp(uri, "http://", 7) != 0)
		return (-1);

	/* Extract the host name. */
	const char *host_begin = uri + 7;
	const char *host_end = strpbrk(host_begin, ":/ \r\n");
	if (host_end == NULL)
		host_end = host_begin + strlen(host_begin);
	int len = host_end - host_begin;
	char *hostname = Malloc(len + 1);
	strncpy(hostname, host_begin, len);
	hostname[len] = '\0';
	*hostnamep = hostname;

	/* Look for a port number.  If none is found, use port 80. */
	if (*host_end == ':') {
		port_begin = host_end + 1;
		port_end = strpbrk(port_begin, "/ \r\n");
		if (port_end == NULL)
			port_end = port_begin + strlen(port_begin);
		len = port_end - port_begin;
	} else {
		port_begin = "80";
		port_end = host_end;
		len = 2;
	}
	char *port = Malloc(len + 1);
	strncpy(port, port_begin, len);
	port[len] = '\0';
	*portp = port;

	/* Extract the path. */
	if (*port_end == '/') {
		pathname_begin = port_end;
		const char *pathname_end = strpbrk(pathname_begin, " \r\n");
		if (pathname_end == NULL)
			pathname_end = pathname_begin + strlen(pathname_begin);
		len = pathname_end - pathname_begin;
	} else {
		pathname_begin = "/";
		len = 1;
	}
	char *pathname = Malloc(len + 1);
	strncpy(pathname, pathname_begin, len);
	pathname[len] = '\0';
	*pathnamep = pathname;

	return (0);
}

/*
 * Requires:
 *   The parameter "sockaddr" must point to a valid sockaddr_in structure.  The
 *   parameter "uri" must point to a properly NUL-terminated string.
 *
 * Effects:
 *   Returns a string containing a properly formatted log entry.  This log
 *   entry is based upon the socket address of the requesting client
 *   ("sockaddr"), the URI from the request ("uri"), and the size in bytes of
 *   the response from the server ("size").
 */
static char *
create_log_entry(const struct sockaddr_in *sockaddr, const char *uri, int size)
{

	/*
	 * Create a large enough array of characters to store a log entry.
	 * Although the length of the URI can exceed MAXLINE, the combined
	 * lengths of the other fields and separators cannot.
	 */
	const size_t log_maxlen = MAXLINE + strlen(uri);
	char *const log_str = Malloc(log_maxlen + 1);

	/* Get a formatted time string. */
	time_t now = time(NULL);
	int log_strlen = strftime(log_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z: ",
	    localtime(&now));

	/*
	 * Convert the IP address in network byte order to dotted decimal
	 * form.
	 */
	Inet_ntop(AF_INET, &sockaddr->sin_addr, &log_str[log_strlen],
	    INET_ADDRSTRLEN);
	log_strlen += strlen(&log_str[log_strlen]);

	/*
	 * Assert that the time and IP address fields occupy less than half of
	 * the space that is reserved for the non-URI fields.
	 */
	assert(log_strlen < MAXLINE / 2);

	/*
	 * Add the URI and response size onto the end of the log entry.
	 */
	snprintf(&log_str[log_strlen], log_maxlen - log_strlen, " %s %d", uri,
	    size);

	printf("LOG STRING IS: %s\n", log_str);

	return (log_str);
}

/*
 * Requires:
 *   The parameter "fd" must be an open socket that is connected to the client.
 *   The parameters "cause", "short_msg", and "long_msg" must point to properly 
 *   NUL-terminated strings that describe the reason why the HTTP transaction
 *   failed.  The string "short_msg" may not exceed 32 characters in length,
 *   and the string "long_msg" may not exceed 80 characters in length.
 *
 * Effects:
 *   Constructs an HTML page describing the reason why the HTTP transaction
 *   failed, and writes an HTTP/1.0 response containing that page as the
 *   content.  The cause appearing in the HTML page is truncated if the
 *   string "cause" exceeds 2048 characters in length.
 */
static void
client_error(int fd, const char *cause, int err_num, const char *short_msg,
    const char *long_msg)
{
	char body[MAXBUF], headers[MAXBUF], truncated_cause[2049];

	assert(strlen(short_msg) <= 32);
	assert(strlen(long_msg) <= 80);
	/* Ensure that "body" is much larger than "truncated_cause". */
	assert(sizeof(truncated_cause) < MAXBUF / 2);

	/*
	 * Create a truncated "cause" string so that the response body will not
	 * exceed MAXBUF.
	 */
	strncpy(truncated_cause, cause, sizeof(truncated_cause) - 1);
	truncated_cause[sizeof(truncated_cause) - 1] = '\0';

	/* Build the HTTP response body. */
	snprintf(body, MAXBUF,
	    "<html><title>Proxy Error</title><body bgcolor=""ffffff"">\r\n"
	    "%d: %s\r\n"
	    "<p>%s: %s\r\n"
	    "<hr><em>The COMP 321 Web proxy</em>\r\n",
	    err_num, short_msg, long_msg, truncated_cause);

	/* Build the HTTP response headers. */
	snprintf(headers, MAXBUF,
	    "HTTP/1.0 %d %s\r\n"
	    "Content-type: text/html\r\n"
	    "Content-length: %d\r\n"
	    "\r\n",
	    err_num, short_msg, (int)strlen(body));

	/* Write the HTTP response. */
	if (rio_writen(fd, headers, strlen(headers)) != -1)
		rio_writen(fd, body, strlen(body));
}

// Prevent "unused function" and "unused variable" warnings.
static const void *dummy_ref[] = { client_error, create_log_entry, dummy_ref,
    parse_uri };

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
