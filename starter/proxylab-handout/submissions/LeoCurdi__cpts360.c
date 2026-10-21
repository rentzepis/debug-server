#include <stdio.h>
#include <string.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define BUFFER_SIZE 5000

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void handleClientRequest(int clientFileDescriptor) {
    rio_t requestRio;
    char requestLine[BUFFER_SIZE], method[100], httpVersion[100], portNumber[10], 
         headerString[BUFFER_SIZE] = "", serverResponse[BUFFER_SIZE] = "", buffer[BUFFER_SIZE],
         requestUri[BUFFER_SIZE], hostName[BUFFER_SIZE];
    char *pRequestUri, *pTemp; // use a char * for uri for string parsing
    int result, count, serverSocketDescriptor;

    // initialize a rio_t struct for reading from the file descriptor
    rio_readinitb(&requestRio, clientFileDescriptor);

    // initialize the rio buffer memory to zeros
    memset(requestRio.rio_buf, 0, BUFFER_SIZE);
    
    // robustly reads a line of text from the rio that contains the client file descriptor
    result = rio_readlineb(&requestRio, requestLine, BUFFER_SIZE);
    if (result <= 0) return;
    
    // parse the request line. Since it is formatted into 3 parts, we can use formatted string scan
    printf("printing request . . . %s", requestLine);
    sscanf(requestLine, "%s %s %s", method, requestUri, httpVersion);
    printf("parsed request: method - %s, request uri - %s, http version - %s\n", method, requestUri, httpVersion);

    // error handling for non GET requests
    if (strcasecmp("GET", method) != 0) return;

    // parse the request uri. we need scheme (http), host name (blank.com), port number (80 for http), and query parameters
    // http://hostname.com:8080/path/to/resource?query=example
    // hostname.com/path/to/resource?query=example

    // use a char pointer for parsing
    pRequestUri = requestUri;
    
    // if uri contains "http://", skip past it
    if (strncmp("http://", pRequestUri, 7) == 0) { // compare the first 7 bytes of the uri
        pRequestUri += 7; // shift the uri pointer 7 bytes
        printf("skipped \"http://\". new uri: %s\n", pRequestUri);
    }

    // get the host name. (go until theres a '/' or ':')
    count = 0;
    pTemp = pRequestUri; // use a temp pointer to not lose the main uri pointer
    while (*pTemp != '/' && *pTemp != ':') {
        count++; // get the length of the host name
        pTemp++;
    } 
    memset(hostName, '\0', BUFFER_SIZE); // initialize the host name memory with null chars
    strncpy(hostName, pRequestUri, count); // copy the host name over from the uri
    pRequestUri += count; // update the pointer
    printf("host name: %s\n", hostName);

    // get the port number
    if (*pRequestUri == ':') { // if port number is included in the uri, it is after ':'
        pRequestUri++; // skip the colon
        count = 0;
        pTemp = pRequestUri; // use a temp pointer to not lose the main uri pointer
        while (*pTemp >= '0' &&  *pTemp <= '9') {
            count++; // get the length of the host name
            pTemp++;
        }
        memset(portNumber, '\0', 10); // initialize the port number memory with null chars
        strncpy(portNumber, pRequestUri, count); // copy the port number over from the uri
        pRequestUri += count; // update the pointer
    }
    else { // if not included - default port number for http is 80
        strcpy(portNumber, "80");
    }
    printf("port number: %s\n", portNumber);

    // the remainder of the request uri contains the query
    printf("query: %s\n", pRequestUri);
    // if the query is blank, slap in a '/' for root
    if (strcmp(pRequestUri, "") == 0) strcpy(pRequestUri, "/");

    // build the header string
    strcat(headerString, "GET ");
    strcat(headerString, pRequestUri); // add the query
    strcat(headerString, " HTTP/1.0\r\n");
    // add in connection, proxy-connection, host, user agent
    strcat(headerString, "Connection: close\r\n");
    strcat(headerString, "Proxy-Connection: close\r\n");
    strcat(headerString, "Host: ");
    strcat(headerString, hostName);
    strcat(headerString, "\r\n");
    strcat(headerString, user_agent_hdr);

    // put in any remaining headers via direct copying
    // read in each field, make sure its not one of the 4 required ones (we already put those in)
    // while there are still fields being read from the request
    while (1) {
        // read a line from the request rio to a buffer
        result = rio_readlineb(&requestRio, buffer, BUFFER_SIZE);
        if (result <= 0) break;

        // check if its one of the 4
        if (strncasecmp("Host:", buffer, strlen("Host:")) && 
            strncasecmp("User-Agent:", buffer, strlen("User-Agent:")) &&
            strncasecmp("Connection:", buffer, strlen("Connection:")) &&
            strncasecmp("Proxy-Connection:", buffer, strlen("Proxy-Connection:"))) {
            // not one of the 4. slap it in
            strcat(headerString, buffer);
        }
        // otherwise - ignore it
        //printf("printing buffer . . . %s", buffer);
        if (strcmp(buffer, "\r\n") == 0 || strcmp(buffer, "\n") == 0) break; // prevents infinite loops
    }
    printf("-------------------------------------------------\n");
    printf("header string: %s\n", headerString);

    // add empty line at the end 
    strcat(headerString, "\r\n");

    // open the connection to the server at the hostname, port
    serverSocketDescriptor = Open_clientfd(hostName, portNumber);
    if (serverSocketDescriptor <= 0) return;

    // write the header string to the server file descriptor
    //rio_readinitb(&requestRio, serverSocketDescriptor); // initialize the rio_t struct for reading from the server file descriptor
    result = rio_writen(serverSocketDescriptor, headerString, strlen(headerString)); // robustly write all n bytes of the header string to the file descriptor
    if (result <= 0) return;

    // read the response from the server, and write it back to the client
    while (1) {
        // we can read the whole response using readn. returns the number of bytes read
        result = rio_readn(serverSocketDescriptor, serverResponse, BUFFER_SIZE);
        if (result <= 0) break;
        result = rio_writen(clientFileDescriptor, serverResponse, result); // write the n byte line to the client
        if (result <= 0) break;
    }

    // close the server file descriptor
    Close(serverSocketDescriptor);
}

/*
    Sets up the proxy server by listening on the entered port and
    continuously accepting incomming connections.
*/
int main(int argc, char **argv) {
    int listeningSocket, clientListeningSocket;
    struct sockaddr clientAddress;
    socklen_t clientAddressLength = sizeof(clientAddress); // allocate space for size of sockaddr struct
    char hostName[BUFFER_SIZE], serverNumber[BUFFER_SIZE];

    // ignore sigpipe errors
    signal(SIGPIPE, SIG_IGN);

    // if user didnt enter the port number
    if (argc < 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    // call Open_listenfd(char *port) from csapp.c
    listeningSocket = Open_listenfd(argv[1]); // arg 1 is the target port number

    // infinite program loop
    while (1) {
        // accept incoming connection
        // takes the socket that the server is listening on, a pointer to the clientAddress, and the length of the client address.
        // returns a new file descriptor for the connection to the client
        clientListeningSocket = Accept(listeningSocket, &clientAddress, &clientAddressLength);
        
        // translate connection address into host name and server number to print the info
        Getnameinfo(&clientAddress, clientAddressLength, hostName, sizeof(hostName), serverNumber, sizeof(serverNumber), 0);
        printf("\n- - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - \n");
        printf("Connected to %s:%s\n", hostName, serverNumber);

        // send the client file descriptor to handle the request
        handleClientRequest(clientListeningSocket);

        // close the file associated with the file descriptor
        Close(clientListeningSocket);
    }
    
    printf("%s", user_agent_hdr);
    return 0;
}
