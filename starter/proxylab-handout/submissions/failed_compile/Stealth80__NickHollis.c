/*
 * proxy.c - CS:APP Web proxy
 *
 * TEAM MEMBERS: 
 *     Nick Hollis, nick.hollis@uky.edu 
 *     Josh Tuschl, jatu228@uky.edu 
 * 
 * This is a web proxy with features to cache both DNS entries and web pages. On first load of a site, a DNS lookup occurs and a page request from the host is made.
 * The results of this request are stored and further requests from a hostname will used the stored DNS and further page requests for the same page will be served
 * from the stored cache.
 */ 

#include "csapp.h"
#include "stdio.h"

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, int  *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size, char* pageCachedStatus);
int handle_request(int connfd, struct sockaddr_in *sockaddr);
int checkIfPageCached();
int checkIfIPCached(char* hostname);
void sigchld_handler(int sig);
int Openclientfd(char *hostname, int port);
int openclientfd(char *hostname, int port);
int checkFileLength();

//structure to store a DNS entry and map it to the host name
struct DNSCache {
	char hostName[MAXLINE];
	struct hostent *hp;
};

//structure to store a page and map it to a host name and path
struct cachePage {
	char cachedHostName[MAXLINE];
	char cachedPathName[MAXLINE];
	char filename[1024];
};

struct DNSCache DNSCaches[1024];		//array to hold DNS caches
struct cachePage cachedPages[1024];		//array to hold page caches
int fileCount = 0;						//number of files stored in the page cache
int isPageCached = -1;					//location of a page in the array if the lookup finds it was cached
int hostsCached = 0;					//number of DNS entries cached
int isIPCached = -1;					//location of a DNS entry in the DNS array if found

int port; //port requested by user
rio_t rio; //a file descriptor for the Rio package
size_t n; //amount of data read from the client
char buf[MAXLINE], uri[MAXLINE], version[MAXLINE], method[MAXLINE]; //a buffer to read client data and a set of variables to parse the buffer into
char hostname[MAXLINE]; // the host name resolved from the uri
char pathname[MAXLINE]; // the path name resolved from the uri
int serverfd; //the file descriptor for the connection from the proxy to the host server
char status[36] = ""; //a placeholder for status messages to be added for the log

//Constants for Log Entries
const char* HOSTCACHED = "(HOSTNAME CACHED)";	
const char* PAGECACHED = "(PAGE CACHED)";
const char* NOTFOUND = "(NOTFOUND)";
const char* NOTCACHED = "(ADDED TO CACHE)";

/* 
 * main - Main routine for the proxy program 
 * listens for connections and if connection request is found,
 * accepts connection, scans input from client and checks method.
 * This proxy only accepts GET requests, others are invalid methods.
 * It then parses the uri so it can extract the host name and 
 * checks if the page and DNS are cached yet or not, forks and 
 * child then passes it off to handle_request.  Parent keeps 
 * listening for connections.
 */
int main(int argc, char **argv)
{     
	int listenfd, connfd, clientlen; //listenfd for listening descriptor, connfd for connected descriptor
	struct sockaddr_in clientaddr;

    /* Check arguments */
    if (argc != 2) {
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
    }

	port = atoi(argv[1]);  //listens on port passed on the command line
	Signal(SIGCHLD, sigchld_handler);
	listenfd = Open_listenfd(port);   //listening descriptor

	while(1) {
		clientlen = sizeof(clientaddr);
		connfd = Accept(listenfd, (SA *)&clientaddr, (socklen_t *) &clientlen);   //Accept connection, returns connection file descriptor

		Rio_readinitb(&rio, connfd); //connection to client for reading, creates a read buffer
		n = Rio_readlineb(&rio, buf, MAXLINE); //read from client
		sscanf(buf, "%s %s %s", method, uri, version);  //scan input from client and extract method, uri, and version
		if (strcmp(method, "GET") != 0) //if method is not GET, return error for invalid method
		{
			printf("%s is not a valid method. \n", method);  //prints to console 
			char outputLine1[MAXLINE]; 
			char outputLine2[MAXLINE]; 
			char outputLine3[MAXLINE];

			//error message to send out connection fd for invalid method
			sprintf(outputLine1, "HTTP/1.1 400 OK\n");   
			sprintf(outputLine2, "Content-Type: text/html; charset=ISO-8859-1\n");
			sprintf(outputLine3, "Connection: close\n\n");

			//write error message to client
			Write(connfd, outputLine1, strlen(outputLine1));
			Write(connfd, outputLine2, strlen(outputLine2));
			Write(connfd, outputLine3, strlen(outputLine3));

			Close(connfd);
		}
		else {
			parse_uri(uri, hostname, pathname, &port);  //call parse_uri to extract host name, path name, and port
			if (fileCount == 1024) {      //max filecount is 1024.  So if file count reaches 1024, reset filecount to zero
				fileCount = 0;
			}

			//check if page is cached
			isPageCached = checkIfPageCached();

			if (hostname == NULL) {   //if host null, then print invalid to console
				printf("Invalid host name.\n");
			}
			else {
				if ((serverfd = Openclientfd(hostname, port)) < 0) //if Openclient returns less than 0, then host was not found
				{
					char logstring[MAXLINE];
					strcpy(status, NOTFOUND);
					//write to log file
					format_log_entry(logstring, &clientaddr, uri, 0, status);
				}
				else { //connection was good
					char fileCountAsChar[4];
					sprintf(fileCountAsChar, "%d", fileCount); //convert fileCount variable to a char array to use as filename

					//add file to the cache
					strcpy(cachedPages[fileCount].cachedHostName, hostname);
					strcpy(cachedPages[fileCount].cachedPathName, pathname);
					strcpy(cachedPages[fileCount].filename, fileCountAsChar);

					if (fork() == 0) { //if child
						Close(listenfd); //close listen socket
					
						if (handle_request(connfd, &clientaddr) < 0) //handle request
						{
							printf("Error Handling Request");
						}
						exit(0);  //on exit will close remaining fd and child ends
					}
					else  //if parent
					{
						Close(connfd);  //close connection fd
						Close(serverfd);  //close server fd
						fileCount++; //increase file count
					}
				}
			}
		}
	}

    exit(0);   //should never get here
}

/* handle_request checks if main found the page was cached or not.
 * if it was, then it sends cached page to client.  If not, requests 
 * page from server and caches page.  Then closes all connections
 * and calls format_log_entry to write to the log.
 */
int handle_request(int connfd, struct sockaddr_in *sockaddr)
{
	int bufSize=0; //total size of data written to client
	char logstring[MAXLINE]; //char array for log entry
	char msg[MAXLINE]; //char array of data returned by host
	size_t m; //length of data returned by host
	FILE *fp; //file pointer to log file
	FILE *cachedfp; //file pointer to a cached file
	int cachedfd; //file descriptor to send data out to client
	int fileGood = -1; //boolean that signifies whether cached file is valid before sending to client

	if (isPageCached > -1) {  //checks if page is cached (assigned in main)
		fileGood = checkFileLength();  //checks file length
	}

	//if file is good and length not zero, then send cached page
	if(fileGood > -1) 
	{
		char fileLocationAsChar[4];
		sprintf(fileLocationAsChar, "%d", isPageCached);
		cachedfd = open(fileLocationAsChar, O_RDONLY);  //open cached file
		if (cachedfd > -1)
		{
			if (strlen(status) == 0) {  //if status empty, string copy status message
				strcpy(status, PAGECACHED);
			}
			else { //if status not empty, concatenate status message
				strcat(status, PAGECACHED);
			}
			printf("File %s was output from cache\n", fileLocationAsChar);
			dup2(cachedfd, serverfd);  //write cached file to client
		}
	}
	else //if not cached
	{
		char serverRequestLine1[MAXLINE];
		char serverRequestLine2[MAXLINE];
		char serverRequestLine3[MAXLINE];
		char serverRequestLine4[MAXLINE];

		//create host request
		sprintf(serverRequestLine1, "%s /%s HTTP/1.1\n", method, pathname);
		sprintf(serverRequestLine2, "Host:%s\n", hostname);
		sprintf(serverRequestLine3, "Connection: close\n");
		sprintf(serverRequestLine4, "User-Agent: Mozilla / 5.0 (Windows NT 6.1; WOW64; rv:25.0) Gecko / 20100101 Firefox / 25.0\n");

		//send request by line
		Write(serverfd, serverRequestLine1, strlen(serverRequestLine1));
		Write(serverfd, serverRequestLine2, strlen(serverRequestLine2));
		Write(serverfd, serverRequestLine3, strlen(serverRequestLine3));
		Write(serverfd, serverRequestLine4, strlen(serverRequestLine4));
		Write(serverfd, "\n", 1);

		Rio_readinitb(&rio, serverfd);  
		printf("Data received from server\n");  //print to console

		//create file for caching
		char fileCountAsChar[4];
		sprintf(fileCountAsChar, "%d", fileCount);
		cachedfp = fopen(fileCountAsChar, "w");

		//add status message for log
		if (strlen(status) == 0) {
			strcpy(status, NOTCACHED);
		}
		else {
			strcat(status, NOTCACHED);
		}

	}
		
	m = 1; //start m at 1 for first read
	while (m > 0) {										//while data still coming in
		m = Read(serverfd, msg, MAXLINE);				//read from server			

		//check if page was not cached, if so write out received data to file
		if (isPageCached < 0) {                         
			fprintf(cachedfp, "%s", msg);
		}

		//write out received data to client
		Write(connfd, msg, m); 

		/*sum the total number of bytes written */
		bufSize += m;
	}

	if (isPageCached < 0) { //if page was not cached, close fp that was written to
		fclose(cachedfp);
	}
	else //if file was cached
	{
		Close(cachedfd);    //otherwise just close fd
	}
	Close(connfd); //close connection fd


	//write log entry to log file
	format_log_entry(logstring, sockaddr, uri, bufSize, status);
	//printf("%s\n",logstring);
	fp = fopen("proxy.log", "a");
	if (!fp) {                     //check log opens properly
		printf("Log not written!"); //if not print to server
	}
	else {
		fprintf(fp, "%s\n", logstring);   //otherwise write to log
		fclose(fp);
	}

	Close(serverfd);  //close server fd
	
	return 0;
}

//checkIfPageCached   traverses all cached files and checks pathname and hostname for a match
int checkIfPageCached() { 
	int fileLocation = -1;
	int index;
	for (index = 0; index < fileCount; index++) {
		if (!strcmp(cachedPages[index].cachedHostName, hostname)) {
			if (!strcmp(cachedPages[index].cachedPathName, pathname)) {
				fileLocation = index;
			}
		}
	}	
	return fileLocation;
}

//checkIfIPCached iterates through DNS caches to see if hostname has been cached in DNS
int checkIfIPCached(char* hostname) {
	int i;
	for (i = 0; i < hostsCached; i++) {
		if (strcmp(hostname, DNSCaches[i].hostName) == 0) {
			isIPCached = 1;
			return i;
		}
	}
	return -1;
}

//sigchld handler kills zombie processes
void sigchld_handler(int sig) {
	while (waitpid(-1, 0, WNOHANG) > 0)
		;
	return;
}


//Openclientfd is passed hostname and port, checks if was stored in cache, and if not, 
//caches it and then connects to the server 
int openclientfd(char *hostname, int port)
{
	int clientfd; //fd to host
	struct hostent *hp; //DNS structure 
	struct sockaddr_in serveraddr; //TCP structure

	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		return -1; /* check errno for cause of error */

	struct timeval timeout; //struct used in setsockopt to define a timeout
	timeout.tv_sec = 10; //length of time needed for a timeout to occur
	timeout.tv_usec = 0; // start of timer

	if (setsockopt(clientfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
		sizeof(timeout)) < 0)
		printf("setsockopt failed\n");
	int cachedIPLocation = checkIfIPCached(hostname);  //check if IP cached
	if (cachedIPLocation > -1) {  //if found, set hostent to cached DNS
		hp = DNSCaches[cachedIPLocation].hp;
		printf("DNS was found in cache\n");
	}
	else {
		/* Fill in the server's IP address and port */
		if ((hp = gethostbyname(hostname)) == NULL) {
			return -2; /* check h_errno for cause of error */
		}
		hostsCached++;
		//fill struct with host to cache
		strcpy(DNSCaches[hostsCached].hostName, hostname);
		DNSCaches[hostsCached].hp = hp;
		printf("DNS was added to cache\n");
	}
	bzero((char *)&serveraddr, sizeof(serveraddr)); //zero out variable
	serveraddr.sin_family = AF_INET; //set protocol
	bcopy((char *)hp->h_addr_list[0],(char *)&serveraddr.sin_addr.s_addr, hp->h_length); //copy first address in hp to address in serveraddr
	serveraddr.sin_port = htons(port); //set port number

	/* Establish a connection with the server */
	if (connect(clientfd, (SA *)&serveraddr, sizeof(serveraddr)) < 0)
		return -1;
	return clientfd;
}

//Openclientfd uses openclientfd to open client fd and report error 
//if unsuccessful and returns openclientfd result
int Openclientfd(char *hostname, int port)
{
	int rc;

	if ((rc = openclientfd(hostname, port)) < 0) {
		if (rc == -1)
			unix_error("Open_clientfd Unix error");
		else
			dns_error("Open_clientfd DNS error");
	}
	return rc;
}

/*
 * parse_uri - URI parser
 * 
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, int *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
	hostname[0] = '\0';
	return -1;
    }
       
    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 80; /* default */
    if (*hostend == ':')   
	*port = atoi(hostend + 1);
    
    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
	pathname[0] = '\0';
    }
    else {
	pathbegin++;	
	strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size, char* pageCachedStatus)
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

	const char* DNSCachedStatus;

	if (isIPCached > -1) {
		DNSCachedStatus = HOSTCACHED;
	}
	else {
		DNSCachedStatus = "";
	}


    /* Return the formatted log entry string */

	sprintf(logstring, "%s: %d.%d.%d.%d %s %d %s %s", time_str, a, b, c, d, uri, size, DNSCachedStatus, pageCachedStatus);
	
}

int checkFileLength() {

	FILE *filePtr;
	int fileSize;

	filePtr = fopen(cachedPages[isPageCached].filename, "r");
	//read to end of file
	fseek(filePtr, 0, SEEK_END);
	//find length of file
	fileSize = ftell(filePtr);
	//move file pointer back to beginning
	rewind(filePtr);
	fclose(filePtr);
	return fileSize > 0 ? fileSize : -1;

}


