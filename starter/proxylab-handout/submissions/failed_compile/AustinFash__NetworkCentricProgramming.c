/*
 * proxy.c - A Simple Sequential Web proxy
 *
 * Course Name: 14:332:456-Network Centric Programming
 * Assignment 2
 * Student Name: Austin Fash
 *
 * This proxy accepts one connection at a time, reads an HTTP GET request,
 * parses the URI to determine the hostname, port, and path, connects to
 * the end server to forward a rewritten GET request, relays the server's
 * response back to the client, and logs the transaction in "proxy.log".
 */

 #include "csapp.h"

 /* function prototype for the provided logging function */
 void format_log_entry(char *logstring, struct sockaddr_in *clientaddr, 
                       char *uri, int size);
 
 /* additional function prototypes */
 void doit(int connfd, struct sockaddr_in *clientaddr);
 void parse_uri(char *uri, char *hostname, char *path, int *port);
 
 /*
  * main - Main routine for the proxy program
  *        
  */
 int main(int argc, char **argv)
 {
     int listenfd, connfd;
     socklen_t clientlen;
     struct sockaddr_in clientaddr;
 
     if (argc != 2) {
         fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
         exit(0);
     }
 
     listenfd = Open_listenfd(atoi(argv[1]));
     while (1) {
         clientlen = sizeof(clientaddr);
         connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
         doit(connfd, &clientaddr);
         Close(connfd);
     }
     exit(0);
 }
 
 /*
  * doit - Handles one HTTP request/response transaction.
  *        It reads the request from the client, parses the URI,
  *        establishes a connection to the end server, forwards the
  *        rewritten request, relays the response back to the client,
  *        and logs the transaction.
  */
 void doit(int connfd, struct sockaddr_in *clientaddr)
 {
     int serverfd, port;
     char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
     char hostname[MAXLINE], path[MAXLINE];
     rio_t client_rio, server_rio;
     int n, total_size = 0;
     char request_buf[MAXLINE];
 
     /* initialize Rio buffer for reading client request */
     Rio_readinitb(&client_rio, connfd);
 
     /* read the request line (e.g., "GET http://www.example.com/index.html HTTP/1.0") */
     if ((n = Rio_readlineb(&client_rio, buf, MAXLINE)) <= 0)
         return;
     sscanf(buf, "%s %s %s", method, uri, version);
     
     /* only support GET method */
     if (strcasecmp(method, "GET")) {
         fprintf(stderr, "Proxy does not implement the method: %s\n", method);
         return;
     }
 
     /* parse the URI to extract hostname, path, and port */
     parse_uri(uri, hostname, path, &port);
 
     /* build the HTTP request for the end server*/
     sprintf(request_buf, "GET %s HTTP/1.0\r\n", path);
     sprintf(buf, "Host: %s\r\n", hostname);
     strcat(request_buf, buf);
     strcat(request_buf, "Connection: close\r\n");
     strcat(request_buf, "Proxy-Connection: close\r\n");
     strcat(request_buf, "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0) Gecko/20100101 Firefox/10.0\r\n");
     strcat(request_buf, "\r\n");
 
     /* open connection to the end server */
     serverfd = Open_clientfd(hostname, port);
     Rio_readinitb(&server_rio, serverfd);
     
     /* send the HTTP request to the server */
     Rio_writen(serverfd, request_buf, strlen(request_buf));
     
     /* relay the server's response back to the client */
     while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0) {
         total_size += n;
         Rio_writen(connfd, buf, n);
     }
     Close(serverfd);
 
     /* log the request in the required format to "proxy.log" */
     {
         char log_entry[MAXLINE];
         format_log_entry(log_entry, clientaddr, uri, total_size);
         FILE *log_file = Fopen("proxy.log", "a");
         Fputs(log_entry, log_file);
         Fclose(log_file);
     }
 }
 
 /*
  * parse_uri - parses the URI into hostname, path, and port.
  *             for example, given "http://www.example.com:8080/index.html",
  *             hostname becomes "www.example.com", path becomes "/index.html",
  *             and port becomes 8080. If no port is specified, defaults to 80.
  */
 void parse_uri(char *uri, char *hostname, char *path, int *port)
 {
     char *hostbegin;
     char *hostend;
     char *portbegin;
     int len;
 
     /* set default port and path */
     *port = 80;
     strcpy(path, "/");
 
     /* skip "http://" if it exists */
     if (strncasecmp(uri, "http://", 7) == 0) {
         hostbegin = uri + 7;
     } else {
         hostbegin = uri;
     }
 
     /* find the first '/' after the hostname */
     hostend = strchr(hostbegin, '/');
     if (hostend != NULL) {
         len = hostend - hostbegin;
         strncpy(hostname, hostbegin, len);
         hostname[len] = '\0';
         strcpy(path, hostend);
     } else {
         strcpy(hostname, hostbegin);
     }
 
     /* check if a port number is specified in the hostname */
     portbegin = strchr(hostname, ':');
     if (portbegin != NULL) {
         *port = atoi(portbegin + 1);
         *portbegin = '\0';  // terminate hostname at the colon
     }
 }
 
 /*
  * format_log_entry - Create a formatted log entry in logstring.
  *
  * the inputs are the socket address of the requesting client
  * (clientaddr), the URI from the request (uri), and the size in bytes
  * of the response from the server (size).
  */
 void format_log_entry(char *logstring, struct sockaddr_in *clientaddr, 
                       char *uri, int size)
 {
     time_t now;
     char time_str[MAXLINE];
     unsigned long host;
     unsigned char a, b, c, d;
 
     /* get a formatted time string */
     now = time(NULL);
     strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));
 
     /* convert the IP address in network byte order to dotted decimal form */
     host = ntohl(clientaddr->sin_addr.s_addr);
     a = host >> 24;
     b = (host >> 16) & 0xff;
     c = (host >> 8) & 0xff;
     d = host & 0xff;
 
     /* format the log entry string */
     sprintf(logstring, "%s: %d.%d.%d.%d %s %d\n", time_str, a, b, c, d, uri, size);
 }
 