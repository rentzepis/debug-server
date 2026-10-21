/*
 * proxy.c - CS:APP Web proxy
 * TEAM MEMBERS: 
 *     Ngan Nguyen nguyenh0@sewanee.edu
 *     David Allen allendj0@sewanee.edu
 *     Bob Makazhu makazbr0@sewanee.edu

 * Description: A proxy web server that intercepts client requests,
 * and logs the request infromation to proxy.log
 */ 

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

extern int errno;

char **blocklist; 
int blocklist_size = 0; 

/* Struct definitions */
struct format_args {
    struct sockaddr_in sock;
    int fd;
};

void *thread(void *vargp); 
void build_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio);
int parse_url(char *url, char *hostname, char *pathname, int *port);
int connect_to_end_server(char *hostname, int port);
void format_log_entry(char *logstring, struct sockaddr_in sockaddr, 
		      char *url, int size, int is_malicious);
void print_log(char* log);
void load_blocklist(const char *filename);
int is_blocked(const char *url);


int main(int argc, char **argv) {
    int listenfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;

    struct format_args *formargs; 
    formargs = malloc(sizeof(struct sockaddr_in) + sizeof(int));

    /* Ignores SIGPIPE signal */
    Signal(SIGPIPE, SIG_IGN);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
	    exit(0);
    }

    listenfd = Open_listenfd(argv[1]);

    load_blocklist("blocklist.txt");


    while (1) {
        clientlen = sizeof(formargs->sock);
        formargs->fd = Accept(listenfd, (SA *)&(formargs->sock), &clientlen);

            Getnameinfo((SA *) &(formargs->sock), clientlen, hostname, MAXLINE, port, MAXLINE, 0);
            printf("Accepted connection from (%s, %s)\n", hostname, port);

            /* sequential request */
            thread(formargs);

            // Free the memory allocated for formargs after each request
            free(formargs);
            formargs = malloc(sizeof(struct sockaddr_in) + sizeof(int));

    }
    
    return 0;
}


/* 
 *  handle function calls 
 *  for each threaded connection request.
 */
void *thread(void *vargp) {
    struct format_args *formargs = (struct format_args *)vargp;
    int fd = formargs->fd;
    struct sockaddr_in clientaddr = formargs->sock;

    int end_serverfd = 0;/*the end server file descriptor*/

    char buf[MAXLINE], method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char endserver_http_header[MAXLINE];
    char logdata[MAXLINE];

    /* store the request line arguments */
    char hostname[MAXLINE], path[MAXLINE];
    int port;

    rio_t rio, server_rio;/* Create client and server rio */

    rio_readinitb(&rio, fd); /* Can't error check, void function */
    
    if((rio_readlineb(&rio, buf, MAXLINE)) == -1) {
        close(fd);
        close(end_serverfd);
        fprintf(stderr, "\n\nReading through pipe failed: %s.\n\n", strerror(errno));
        return (void *)0;
    }
    sscanf(buf, "%s %s %s", method, url, version); /* read the client request line */
    printf("URL from client request: %s\n", url);

    if(strcasecmp(method, "GET") != 0){
        printf("\n%s method not supported. Proxy only supports GET method.\n", method);
        return (void *)0;
    }

    /* parse the URL to get hostname, file path, and port */
    if((parse_url(url, hostname, path, &port)) == -1) {
        printf("\nAddress must start with http://\n");
        return (void *)0;
    }
/* handle blocked url requests */
    if (is_blocked(hostname)) {
        char block_message[1024];
        sprintf(block_message, "HTTP/1.1 403 Forbidden\r\nContent-Type: text/html\r\n\r\n"
                            "<html><body><h1>Blocked site</h1><p>Access to %s is blocked.</p>"
                            "</body></html>\r\n", hostname);

        int message_length = strlen(block_message);
        
        //log the blocked url request
        format_log_entry(logdata, clientaddr, url, message_length, 1); //1 for malicious
        
        printf("\n\nLogged: %s\n\n", logdata);

        //send block message to client
        rio_writen(fd, block_message, strlen(block_message));
        Close(fd);
        return (void *)0;
    }

    /* build the http header which will send to the end server */
    build_header(endserver_http_header, hostname, path, port, &rio);

    /* connect to the end server */
    if((end_serverfd = connect_to_end_server(hostname, port)) < 0) {
        close(fd);
        close(end_serverfd);
        fprintf(stderr, "Connection to server failed: %s.\n", strerror(errno));
        return (void *)0;
    }

    rio_readinitb(&server_rio, end_serverfd);/* Can't error check, void function */

    /* write the http header to endserver */
    if((rio_writen(end_serverfd, endserver_http_header, strlen(endserver_http_header))) == -1) {
        close(fd);
        close(end_serverfd);
        fprintf(stderr, "\n\nPipe Failed: %s.\n\n", strerror(errno));
        return (void *)0;
    }

    /* receive message from end server and send to the client */
    int sizebuf = 0;
    size_t n;
    while((n = rio_readlineb(&server_rio, buf, MAXLINE)) != 0) {
        sizebuf += n;
        
        if((rio_writen(fd, buf, n)) == -1) {
            close(fd);
            close(end_serverfd);
            fprintf(stderr, "\n\nPipe failed: %s.\n\n", strerror(errno));
            return (void *)0;
        }
    }

    /* Formats the log entry of the request and sends it to the print_log() function */
    format_log_entry(logdata, clientaddr, url, sizebuf, 0);

    printf("\n\nLogged: %s\n\n", logdata);
    
    Close(end_serverfd); 
    Close(fd);

    return (void *)0;
}

/*
 * parse_url - URL parser
 * 
 * Given a URL from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be aßlocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_url(char *url, char *hostname, char *pathname, int *port) {
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(url, "http://", 7) != 0) {
	    hostname[0] = '\0';
	    return -1;
   }
       
    /* Extract the host name */
    hostbegin = url + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';
    
    /* Extract the port number */
    *port = 15200; /* default port */
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
 * takes the HTTP header, the hostname, the path of the website,
 * the port number of the client, and the client's rio and
 * buid the reformatted HTTP header into the desired HTTP/1.0 request.
 * similar to tiny.c
 */
void build_header(char *http_header, char *hostname, char *path, int port, rio_t *client_rio) {
    char buf[MAXLINE], request_header[MAXLINE], other_header[MAXLINE], host_header[MAXLINE];
    
    /* Predefined request parts that we check and build properly */
    const char *user_agent_header = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    const char *conn_header = "Connection: close\r\n";
    const char *proxy_header = "Proxy-Connection: close\r\n";
    const char *endof_header = "\r\n";   
    const char *connection_key = "Connection";
    const char *user_agent_key= "User-Agent";
    const char *proxy_connection_key = "Proxy-Connection";
    const char *host_key = "Host";

    sprintf(request_header, "GET /%s HTTP/1.0\r\n", path);

    /* get other request header for client rio and change it */
    while(rio_readlineb(client_rio, buf, MAXLINE) > 0) {
        if(strcmp(buf, endof_header) == 0) break; 

        /* Check if buf contains Host: */
        if(!strncasecmp(buf, host_key, strlen(host_key))) {
            //Copy buf to host_header
            strcpy(host_header, buf);
            continue;
        }

        /* 
         * Checking for extra request headers found after 
         * Connection:, Proxy-Connectiion:, and User-Agent: 
        */
        if(!strncasecmp(buf, connection_key, strlen(connection_key))
            && !strncasecmp(buf, proxy_connection_key, strlen(proxy_connection_key))
            && !strncasecmp(buf, user_agent_key, strlen(user_agent_key)))
        {
            /* Copy those extra requests in buf to other_header */
            strcat(other_header, buf);
        }
    }

    /* Check to see if host_header is empty */
    if(strlen(host_header) == 0) {
        /* Copy the host info into host_header */
        sprintf(host_header, "Host: %s\r\n", hostname);
    }

    /* Create the HTTP header from the changed, proper request lines */
    sprintf(http_header,"%s%s%s%s%s%s%s",
            request_header,
            host_header,
            conn_header,
            proxy_header,
            user_agent_header,
            other_header,
            endof_header);

    return;
}


/* 
 * connect_to_end_server - connects to the end server 
 * connect that client to the end server they are attempting to request information
 * from.  Returns -1 or -2 on error (see csapp.c open_clientfd() function definition).
 */
int connect_to_end_server(char *hostname, int port) {

    char portStr[100];

    /* Copy the port number to a string */
    sprintf(portStr, "%d", port);

    /* Return the int representing if the connection was successfully made */
    return open_clientfd(hostname, portStr);

}


/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URL from the request (url), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in sockaddr, 
		      char *url, int size, int is_malicious) {
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* Calculate the parts of the URL */
    char *pos = strstr(url,"//");
    pos = pos != NULL ? pos+2: url;
    char *pos2 = strstr(pos,"/");

    /* Find the size of the needed part of the URL and parse it */
    int buffersize = (strlen(url) - strlen(pos2)) + 2; 
    char urlbase[buffersize];
    strncpy(urlbase, url, buffersize + 1);
    urlbase[sizeof(urlbase) - 1] = '\0';

    /* Retrieve client IP address and change
     * from network to host byte order. this is a thread safe conversion
     */
    host = ntohl(sockaddr.sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Store the formatted log entry string in logstring */
    if (is_malicious) {
        sprintf(logstring, "[MALICIOUS] [%s] %d.%d.%d.%d %s %d\n", time_str, a, b, c, d, urlbase, size);
    } else {
        sprintf(logstring, "[%s] %d.%d.%d.%d %s %d\n", time_str, a, b, c, d, urlbase, size);
    }
    
    /* Log the string in the proxy.log file */
    print_log(logstring);

    return;
}


/* 
 * Logs the log string from format_log_entry to the proxy.log file
 * uses semaphore (mutex) to handle possible thread issues while 
 * write to the file
 */
void print_log(char* log) {
    sem_t mutex;
    if(sem_init(&mutex, 0, 1) < 0) {
        fprintf(stderr, "Could not initialize semaphore in print_log(): %s\n", strerror(errno));
        return;
    }
	
    FILE *proxy_log;
    char *file = "proxy.log";

    P(&mutex); // one log at a time to handle concurrent requests
    if(access(file, F_OK) == 0) {
        // append file to proxy.log
        proxy_log = fopen(file, "a");
    } else {
        // file doesn't exist, create it and add logs to it
        proxy_log = fopen(file, "w+");
    }
    
    fwrite(log, 1, strlen(log), proxy_log);
    fclose(proxy_log);
    V(&mutex);
}

/*
 * Loads blocklist from file into a dynamically allocated array.
 * Each line is assumed to be one URL. If the file cannot be opened it returns
 * without modifying the blocklist. 
 */
void load_blocklist(const char *filename) {
    char buf[MAXLINE];
    FILE *fp = fopen(filename, "r");
    if (!fp) return; // File not found, continue without blocklist

    while (fgets(buf, MAXLINE, fp) != NULL) {
        blocklist_size++;
        blocklist = realloc(blocklist, blocklist_size * sizeof(char *));
        buf[strlen(buf) - 1] = '\0'; // Remove newline character
        blocklist[blocklist_size - 1] = strdup(buf); // Duplicate buf and store the new string pointer in the blocklist array.
    }
    fclose(fp);
}

/*Checks to see if url should be blocked by comparing it to blocklist*/
int is_blocked(const char *url) {
    for (int i = 0; i < blocklist_size; i++) {
        if (strcmp(blocklist[i], url) == 0) {
            return 1;  // True
        }
    }
    return 0;  // False
}
