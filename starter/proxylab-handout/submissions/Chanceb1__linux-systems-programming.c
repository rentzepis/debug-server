#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


void forward_request(int clientfd, const char *request, const char *host) 
{
    // Get IP address of server
    struct hostent *server = gethostbyname(host);
    if (server == NULL) {
        fprintf(stderr, "Error: No such host\n");
        return;
    }

    // Define server address structure
    struct sockaddr_in serveraddr;
    bzero((char *)&serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serveraddr.sin_addr.s_addr, server->h_length);
    serveraddr.sin_port = htons(80); // Default HTTP port

    // Create socket for server connection
    int serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) {
        perror("Error opening socket for server");
        return;
    }

    // Connect to server
    if (connect(serverfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("Error connecting to server");
        close(serverfd);
        return;
    }

    // Forward request to server
    if (send(serverfd, request, strlen(request), 0) < 0) {
        perror("Error sending request to server");
        close(serverfd);
        return;
    }

    // Receive and print server response
    char response[MAXLINE];
    ssize_t n;
    while ((n = recv(serverfd, response, MAXLINE, 0)) > 0) {
        if (write(clientfd, response, n) < 0) {
            perror("Error writing response to client");
            close(serverfd);
            return;
        }
    }

    if (n < 0) {
        perror("Error receiving response from server");
    }

    // Close connection with server
    close(serverfd);
}


// function to handle client request parsing and forwarding
void proxy(int connfd) 
{
    char buf[MAXLINE];
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];
    char host[MAXLINE], path[MAXLINE];
    int serverfd, port;
    ssize_t n;

    // Read request line from client
    n = recv(connfd, buf, MAXLINE, 0);
    if (n <= 0) {
        perror("Error reading from client");
        return;
    }

    // Parse request line
    if (sscanf(buf, "%s %s %s", method, url, version) != 3) {
        fprintf(stderr, "Malformed request\n");
        return;
    }

    // Check if request is GET method
    if (strcmp(method, "GET") != 0) {
        fprintf(stderr, "Only GET requests are supported\n");
        return;
    }

    // Remove "http://" from URL
    char *clean_url = strstr(url, "//");
    if (clean_url != NULL) {
        clean_url += 2;
    } else {
        clean_url = url;
    }

    // Parse URL to extract host and path
    if (sscanf(clean_url, "%[^:/]:%d%s", host, &port, path) != 3) {
        if (sscanf(clean_url, "%[^/]%s", host, path) != 2) {
            fprintf(stderr, "Malformed URL\n");
            return;
        }
    }

    // Parse URL to extract host and path
    // if (sscanf(url, "http://%[^:/]:%d%s", host, &port, path) == 3) 
    // {
    // } else if (sscanf(url, "http://%[^/]%s", host, path) == 2) 
    // {
    //     port = 80; // Default  port
    // } else {
    //     fprintf(stderr, "Malformed URL\n");
    //     return;
    // }

    // build request with headers
    char request[MAXLINE];
    sprintf(request, "GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: %s\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", path, host, user_agent_hdr);

    // Forward request to server
    forward_request(connfd, request, host);
}


int main(int argc, char **argv) 
{
    char client_hostname[MAXLINE], client_port[MAXLINE];
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_in clientaddr; 

    // Check command line args
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    // Validate port number
    int proxy_port = atoi(argv[1]);
    if (proxy_port < 1024 || proxy_port > 65535) {
        fprintf(stderr, "Invalid port number\n");
        exit(1);
    }

    // Create listening socket
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("Error opening socket");
        exit(1);
    }

    // Set up server socket address structure
    struct sockaddr_in serveraddr;
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(proxy_port);

    // Bind socket
    if (bind(listenfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("Error binding socket");
        exit(1);
    }

    // Listen for connections
    if (listen(listenfd, 5) < 0) {
        perror("Error listening on socket");
        exit(1);
    }

    printf("Proxy server running on port %d...\n", proxy_port);

    // Main loop Accepts connections and handle requests 
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = accept(listenfd, (struct sockaddr *)&clientaddr, &clientlen);
        if (connfd < 0) {
            perror("Error accepting connection");
            continue;
        }
        {   // connection accepted message
            Getnameinfo((struct sockaddr *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
            fprintf(stdout, "Connected to (%s), port %s\n", client_hostname, client_port);
        }

        // Call proxy function to handle client request
        proxy(connfd);

        // Close client connection
        close(connfd);
    }

    // Close listening socket
    close(listenfd);

    return 0;
}