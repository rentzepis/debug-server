/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 2013-11431
 *         Name: Hyunjin Jeong
 * 
 * Echo proxy lab - Multi-threaded version.
 * My proxy is a server for client at first. It listens client by Open_listenfd. After it accepts the request,
 * then thread function(process_request) is implemented. In the process_request() function, the proxy reads
 * from client and writes to server. Then it reads upper_case string from server, and writes the string to client.
 * To protect mutual exclusion, I use two semaphores, mutex and filemutex. The mutex locks and copys around 
 * gethostbyname() function, and the filemutex is for the log file.

 * I uses some wrapper functions, such as open_clientfd_ts, Rio_readn_w, etc.
 */ 

#include "csapp.h"
#include "time.h"

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"


/*
 * Functions to define
 */
void *process_request(void* vargp);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);
int isNumber(char *s);
void dolog(char *ip, int port, int size, char *msg);
///

// Semaphores
sem_t mutex;
sem_t filemutex;


// Struct to give many arguments in process_request function.
struct Par {
	int client_port;
	int proxy_port;
	int connfd;
	char client_ip[32];
};
		
/*
 * main - Main routine for the proxy program
 */

int main(int argc, char **argv)
{
	int listenfd, port, clientfd;
	struct sockaddr_in clientaddr;
	int clientlen = sizeof(clientaddr);
	pthread_t tid;
	
	// Initialize proxies.
	Sem_init(&mutex, 0, 1);
	Sem_init(&filemutex, 0, 1);


    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);

    }
	port = atoi(argv[1]);
	
	
	// Ignore SIGPIPE signal.
	Signal(SIGPIPE, SIG_IGN);
	
	// socket(), bind(), listen()
	listenfd = Open_listenfd(port);

	while(1) {
		// allocate memory dynamically to protect thread-safe.
		clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

		struct Par *factor = (struct Par *) Malloc(sizeof(struct Par));
		factor->connfd = clientfd;
		factor->client_port = ntohs(clientaddr.sin_port);
		factor->proxy_port = port;
		strcpy(factor->client_ip, inet_ntoa(clientaddr.sin_addr));

		// Make threads.
		Pthread_create(&tid, NULL, process_request, (void *)factor);
	}
}

// isNumber determines whether string is integer(i.e: 1234) or not.
int isNumber(char *s) {
	int i;
	int size = strlen(s);

	if(size == 0)
		return 0;

	for(i = 0; i < size; i++) {
		if(s[i] < '0' || s[i] > '9')
			return 0;
	}
	return 1;
}

// do_log() writes log informations to "proxy.log" file.
void do_log(char *ip, int port, int size, char *msg) {
	time_t timer;
	struct tm *t;
	char buf[MAXLINE];
	char bufbuf[MAXLINE];

	// Get time and make output format.
	time(&timer);
	t = localtime(&timer);
	strftime(buf, MAXLINE, "%a %d %b %Y %H:%M:%S %Z:", t);

	P(&filemutex);
	int fd = Open(PROXY_LOG, O_RDWR|O_CREAT|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
	// Write to file.
	sprintf(bufbuf, "%s %s %d %d %s", buf, ip, port, size, msg);
	Write(fd, bufbuf, strlen(bufbuf));
	Close(fd);
	V(&filemutex);
	return;
}

// Thread routine.
void *process_request(void *vargp) {
    //Make thread run in detached mode.
    Pthread_detach(pthread_self());

	//Copy contents in struct.
	struct Par *str = (struct Par *)vargp;
	int fd_client = str->connfd;
	int pport = str->proxy_port;
	int cport = str->client_port;
	char *cip = str->client_ip;

	// free struct Par.
	free(vargp);

	rio_t rio_server, rio_client;
	char buf[MAXLINE], host[MAXLINE], port[MAXLINE], msg[MAXLINE];
	int fd_server, port_server, len;

	// token for parsing string.
	char *token = NULL;
	
	// Initialize rio with client
	Rio_readinitb(&rio_client, fd_client);
	
	// while loop(). It is for continous communication.
	while((len = Rio_readlineb_w(&rio_client, buf, MAXLINE)) != 0) { // read form client.
		int tokcnt = 0;

		token = strtok(buf, " ");

		// Parse input string. To this format: <host> <port> <msg>
		while(token != NULL) {
			if(tokcnt == 0)
				strcpy(host, token);
			else if(tokcnt == 1)
				strcpy(port, token);
			else if(tokcnt == 2)
				strcpy(msg, token);
			else if(tokcnt > 2) {
				strcat(msg, " ");
				strcat(msg, token);
			}
			token = strtok(NULL, " ");
			tokcnt++;
		}
		
		if(tokcnt < 3) { // If string has too few arguments.
			strcpy(buf, "proxy usage: <host> <port> <message>\n");
			Rio_writen_w(fd_client, buf, strlen(buf));
		}
		else if(pport == atoi(port)) { // When client requests port of proxy.
			strcpy(buf, "Cannot use proxy port.\n");
			Rio_writen_w(fd_client, buf, strlen(buf));
		}
		else if(!isNumber(port)) { // If port is not number.
			strcpy(buf, "proxy usage: <host> <port> <message>\n");
			Rio_writen_w(fd_client, buf, strlen(buf));
		}
		else {
			strcpy(buf, msg);
			
			port_server = atoi(port);
			
			// open server.
			if((fd_server = open_clientfd_ts(host, port_server, NULL)) < 0) {
				Close(fd_server);
				strcpy(buf, "open_clientfd Error!\n");
				Rio_writen_w(fd_client, buf, strlen(buf));
			}
			else {
				// Initialize rio with server.
				Rio_readinitb(&rio_server, fd_server);
			
				// Write message to server.
				Rio_writen_w(fd_server, buf, strlen(msg));
				// Read message from server.
				Rio_readlineb_w(&rio_server, buf, MAXLINE);
				// Close connection with server.
				Close(fd_server);
				
				// Log to file.
				do_log(cip, cport, strlen(buf), buf);

				// Write uppercase string to client
				Rio_writen_w(fd_client, buf, strlen(buf));
			}
		}
	}
	Close(fd_client);
	return NULL;
}

// Thread-safe version open_clientfd.
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp) {
	int clientfd;
    struct hostent *hp;
    struct sockaddr_in serveraddr;

    if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1; /* check errno for cause of error */

    /* Fill in the server's IP address and port */
	// Lock the mutex.
	P(&mutex);
    if ((hp = gethostbyname(hostname)) == NULL)
		return -2; /* check h_errno for cause of error */
	V(&mutex); // unlock the mutex.
	
    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)hp->h_addr_list[0], (char *)&serveraddr.sin_addr.s_addr, hp->h_length);
    serveraddr.sin_port = htons(port);

    /* Establish a connection with the server */
    if (connect(clientfd, (SA *) &serveraddr, sizeof(serveraddr)) < 0)
		return -1;
    return clientfd;
}

// Wrapper function that doesn't terminate process when error occurs.
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) {
	size_t temp;
	
	if((temp = rio_readn(fd, ptr, nbytes)) < 0) {
		printf("Warning: Rio_readn_w failed!\n");
		return 0;
	}
	
	return temp;
}

// Wrapper function that doesn't terminate process when error occurs.
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) {
	ssize_t temp;

	if((temp = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
		printf("Warning: Rio_readlineb_w failed!\n");
		return 0;
	}
	
	return temp;
}

// Wrapper function that doesn't terminate process when error occurs.
void Rio_writen_w(int fd, void *usrbuf, size_t n) {
	if(rio_writen(fd, usrbuf, n) != n) 
		printf("Warning: Rio_writen_w failed!\n");
}
