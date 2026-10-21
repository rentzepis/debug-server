/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 20130516
 * Name: Lee Hyeonseop
 * Email: protos37@kaist.ac.kr
 * 
 * Nothing special, just typical simple web proxy.
 * I read plenty of documents, especially RFCs about TCP and HTTP.
 * Some detailed features may different from limitation from lab manual but I tried to obey official standards.
 * 
 * Program accepts request, create thread for each incoming request.
 * Then thread recieves first line, parse HTTP request, open connection with end server.
 * Thread relay all header lines looking for "Content-Length".
 * By RFC, thread waits for content only if it's POST request and "Content-length" provided by browser.
 * 
 * After transfering whole request to end server, now thread waits for reply.
 * It recieves first line, parse HTTP response.
 * Thread relay all header lines looking for "Content-Length".
 * By RFC, thread waits for content except for some method and response code.
 * 
 * There are still some issues:
 * Numbers of CLOSE_WAIT, FIN_WAIT_2 remains after whole transfer.
 * And sometimes browser waits long after connection has closed.
 * I think its about FIN and pipe related issues, or maybe ajax or somethings, but anyway, proxy works.
 */ 

#include "csapp.h"
#include "csapp.h"

/*
 * Data Bundle for new thread
 */
struct threaddata
{
	int sockfd;
	FILE *logfd;
	struct sockaddr_in client;
	pthread_t thread;
	pthread_mutex_t *syscall, *logfile;
};

/*
 * Function prototypes
 */
void *process(void *ptr);
int parse_request(char *request, char *method, char *uri, char *hostname, int *port);
int parse_response(char *response, int *msg);
int open_proxyfd(int port);
int open_serverfd(char *hostname, int port, pthread_mutex_t *mutex);
int readline(int sockfd, char *buf, int len);
int writebuf(int sockfd, char *buf, int len);
int relay_header(int from, int to, int *content, char *buf);
int relay_content(int from, int to, int content, char *buf, int len);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

/* 
 * main - Main routine for the proxy program 
 * Initialization, open log file, create mutex, and wait for request.
 * Creates thread for each incoming request.
 */
int main(int argc, char **argv)
{
	int sockfd, newfd, size;
	FILE *logfd;
	struct sockaddr_in client;
	pthread_mutex_t syscall, logfile;
	struct threaddata *data;

	// check arguments
	if (argc != 2)
	{
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}

	// init mutexes
	if(pthread_mutex_init(&syscall, NULL) < 0 || pthread_mutex_init(&logfile, NULL) < 0)
	{
		perror("pthread_mutex_init");
		exit(0);
	}

	// open Listening socket stream
	sockfd = open_proxyfd(atoi(argv[1]));
	if(sockfd < 0)
	{
		exit(0);
	}

	// open log file descriptor
	logfd = fopen("proxy.log", "w");
	if(logfd == NULL)
	{
		close(sockfd);
		exit(0);
	}

	// main loop
	for(; ; )
	{
		// accept proxy request
		if((newfd = accept(sockfd, (struct sockaddr *)&client, (socklen_t *)&size)) < 0)
		{
			perror("accept");
			continue;
		}
		// init data bundle for new thread
		data = malloc(sizeof(struct threaddata));
		data->sockfd = newfd;
		data->logfd = logfd;
		data->syscall = &syscall;
		data->logfile = &logfile;
		memcpy(&data->client, &client, sizeof(struct sockaddr_in));

		// create thread
		if(pthread_create(&data->thread, NULL, process, data))
		{
			free(data);
			perror("pthread_create");
			continue;
		}
	}

	// I don't know when these executed..
	fclose(logfd);
	close(sockfd);
	pthread_mutex_destroy(&syscall);
	pthread_mutex_destroy(&logfile);

	return 0;
}

/* 
 * process - Main routine for proxy thread
 * Handles whole proxy transfers.
 */
void *process(void *ptr)
{
	int sockfd, newfd, len, port, msg, content, sum;
	char buf[MAXLINE], uri[MAXLINE], hostname[MAXLINE], method[MAXLINE];
	struct threaddata *data;

	// catch data bundle
	data = ptr;
	sockfd = data->sockfd;

	// detach thread
	if(pthread_detach(pthread_self()) == 0)
	{
		// recieve first line of request
		len = readline(sockfd, buf, MAXLINE);
		if(0 < len)
		{
			// parse HTTP request
			if(parse_request(buf, method, uri, hostname, &port) == 0)
			{
				// open connection to end server
				newfd = open_serverfd(hostname, port, data->syscall);
				if(0 <= newfd)
				{
					// write request
					writebuf(newfd, buf, len);
					// relay whole header
					relay_header(sockfd, newfd, &content, buf);
					// if POST request and content-length provided
					if(strncasecmp(method, "post", 4) == 0 && content)
					{
						// relay following contents
						relay_content(sockfd, newfd, content, buf, MAXLINE);
					}

					sum = 0;
					// receive first line of response
					len = readline(newfd, buf, MAXLINE);
					if(0 < len)
					{
						sum += len;
						// parse HTTP response
						parse_response(buf, &msg);
						// write response
						writebuf(sockfd, buf, len);
						// relay whole header
						relay_header(newfd, sockfd, &content, buf);
						// if there might be content following header
						if(strncasecmp(method, "head", 4) != 0 && 2 <= msg / 100 && msg != 204 && msg != 304)
						{
							// relay following contents
							len = relay_content(newfd, sockfd, content, buf, MAXLINE);
							if(0 < len)
							{
								sum += len;
							}
						}
					}

					// if anything had transfered to client
					if(sum)
					{
						// lock mutex
						pthread_mutex_lock(data->logfile);
						// write to log file
						format_log_entry(buf, &data->client, uri, sum);
						printf("%s\n", buf);
						fprintf(data->logfd, "%s\n", buf);
						// unlock mutex
						pthread_mutex_unlock(data->logfile);
					}

					// close end server connection
					close(newfd);
				}
			}
		}
	}
	else
	{
		fprintf(stderr, "pthread_detach: failed\n");
	}

	// close client connection
	close(sockfd);
	free(data);

	return NULL;
}

/*
 * parse_request - HTTP request parser
 * Given first line of a HTTP request, parse method, full uri, hostname, port.
 * Return -1 if there are any problems.
 */
int parse_request(char *request, char *method, char *uri, char *hostname, int *port)
{
	int len;
	char *hostbegin, *hostend, *tok, *end;

	// extract method
	tok = strchr(request, ' ');
	strncpy(method, request, tok - request);
	tok++;

	if (strncasecmp(tok, "http://", 7) != 0) {
		hostname[0] = '\0';
		return -1;
	}

	/* Extract the host name */
	hostbegin = tok + 7;
	hostend = strpbrk(hostbegin, " :/\r\n\0");
	len = hostend - hostbegin;
	strncpy(hostname, hostbegin, len);
	hostname[len] = '\0';

	/* Extract the port number */
	*port = 80; /* default */
	if (*hostend == ':')
		*port = atoi(hostend + 1);

	// extract full uri
	end = strchr(tok, ' ');
	strncpy(uri, tok, end - tok);
	*(uri + (end - tok)) = '\0';

	return 0;
}

/*
 * parse_response - HTTP response parser
 * Given first line of a HTTP response, parse response code.
 */
int parse_response(char *response, int *msg)
{
	char *code;

	code = strchr(response, ' ') + 1;
	*msg = atoi(code);

	return 0;
}

/*
 * open_proxyfd - socket wrapper
 * Open listening socket with port
 * Return -1 if there are any problems.
 */
int open_proxyfd(int port)
{
	int sockfd, optval;
	struct sockaddr_in proxy;

	optval = 1;

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket");
		return -1;
	}
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0)
	{
		perror("setsockopt");
		close(sockfd);
		return -1;
	}

	memset(&proxy, 0, sizeof(struct sockaddr_in));
	proxy.sin_family = AF_INET; 
	proxy.sin_addr.s_addr = htonl(INADDR_ANY); 
	proxy.sin_port = htons(port);

	if(bind(sockfd, (struct sockaddr *)&proxy, sizeof(struct sockaddr_in)) < 0)
	{
		perror("bind");
		close(sockfd);
		return -1;
	}
	if(listen(sockfd, LISTENQ) < 0)
	{
		perror("listen");
		close(sockfd);
		return -1;
	}

	return sockfd;
}

/*
 * open_serverfd - socket wrapper
 * Open tcp connection to host:port. Care mutex calling gethostbyaddr
 * Return -1 if there are any problems.
 */
int open_serverfd(char *hostname, int port, pthread_mutex_t *mutex)
{
	int sockfd;
	struct sockaddr_in server;
	struct hostent *host;

	memset(&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if(pthread_mutex_lock(mutex))
	{
		fprintf(stderr, "pthread_mutex_lock: failed\n");
		return -1;
	}
	if((host = gethostbyname(hostname)) == NULL)
	{
		pthread_mutex_unlock(mutex);
		herror("gethostbyname");
		return -1;
	}

	memcpy(&server.sin_addr.s_addr, host->h_addr_list[0], host->h_length);
	pthread_mutex_unlock(mutex);

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		perror("socket");
		return -1;
	}

	if(connect(sockfd, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) < 0)
	{
		perror("connect");
		close(sockfd);
		return -1;
	}

	return sockfd;
}

/*
 * readline - read a line from descriptor
 * Return 0 if EOF.
 * Return -1 if there are any problems.
 */
int readline(int sockfd, char *buf, int len)
{
	int i, res;

	for(i = 0; i < len; i++)
	{
		res = read(sockfd, buf + i, 1);
		if(res == 1)
		{
			if(buf[i] == '\n')
			{
				i++;
				break;
			}
		}
		else
		{
			return res;
		}
	}

	return i;
}

/*
 * writebuf - write given buffer
 * Full transfer guaranteed.
 * Return -1 if there are any problems.
 */
int writebuf(int sockfd, char *buf, int len)
{
	int i, res;
	for(i = 0; i < len; )
	{
		res = write(sockfd, buf + i, len - i);
		if(res == -1)
		{
			return -1;
		}
		i += res;
	}
	return i;
}

/*
 * relay_header - relay HTTP header
 * Empty Line ("\r\n") or EOF are considered end of header.
 * Return -1 if there are any problems.
 */
int relay_header(int from, int to, int *content, char *buf)
{
	int len, sum;

	*content = 0;
	for(sum = 0; ;)
	{
		if((len = readline(from, buf, MAXLINE)) < 0)
		{
			perror("read");
			return -1;
		}
		// look for "Content-Length" item in header
		if(strncasecmp(buf, "Content-Length", 14) == 0)
		{
			*content = atoi(strchr(buf, ' ') + 1);
		}
		if(0 < len)
		{
			if(writebuf(to, buf, len) < 0)
			{
				perror("write");
				return -1;
			}
			sum += len;
		}
		if(len == 0 || strncmp(buf, "\r\n", 2) == 0)
		{
			break;
		}
	}

	return sum;
}

/*
 * relay_content - relay HTTP content
 * Given content length or EOF are considered end of content.
 * Return -1 if there are any problems.
 */
int relay_content(int from, int to, int content, char *buf, int len)
{
	int i, j, res;

	for(i = j = 0; ; i++, j++)
	{
		if(content && i == content)
		{
			break;
		}
		if(j == len)
		{
			if(writebuf(to, buf, j) < 0)
			{
				perror("write");
				return -1;
			}
			j = 0;
		}
		res = read(from, buf + j, 1);
		if(res == -1)
		{
			perror("read");
			return -1;
		}
		if(res == 0)
		{
			break;
		}
	}
	writebuf(to, buf, j);
	return i;
}
/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
											char *uri, int size)
{
	time_t now;
	char time_str[MAXLINE];
	unsigned long host;
	unsigned char a, b, c, d;
	/* Get a formatted time string */
	now = time(NULL);
	strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));
	/* 
	 * Convert the IP address in network byte order to dotted decimal
	 * form. Note that we could have used inet_ntoa, but chose not to
	 * because inet_ntoa is a Class 3 thread unsafe function that
	 * returns a pointer to a static variable (Ch 13, CS:APP).
	 */
	host = ntohl(sockaddr->sin_addr.s_addr);
	a = host >> 24;
	b = (host >> 16) & 0xff;
	c = (host >> 8) & 0xff;
	d = host & 0xff;
	/* Return the formatted log entry string */
	sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}
