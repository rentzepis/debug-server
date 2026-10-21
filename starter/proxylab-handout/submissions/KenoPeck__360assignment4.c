#include "csapp.h"
//#include <features.h>
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
//static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:124.0) Gecko/20100101 Firefox/124.0";

#define BUFFER_SIZE 8192
#define ADDITIONAL_HEADER_LIST_SIZE 2500
#define ADDITIONAL_HEADER_SIZE 512
#define HOSTNAME_SIZE 512
#define PATH_SIZE 1024

void proxy(int client_soc) {
    char buffer[BUFFER_SIZE]; // initial buffer for client request
    bzero(buffer, BUFFER_SIZE);

    rio_t client_rio;
    int readBytes = 0;

    rio_readinitb(&client_rio, client_soc); // Get initial client request
    int n = rio_readnb(&client_rio, buffer, BUFFER_SIZE - 1);
    if (n <= -1){
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "!!!!RIO Error: Couldn't Read from Client Socket!!!!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(client_soc);
        return;
    }

    if (!(buffer[0] == 'G' && buffer[1] == 'E' && buffer[2] == 'T' && buffer[3] == ' ')){ // Check request type and make sure its a GET
        char* rTypeStart = buffer, *rTypeEnd = strstr(buffer, " ");
        char rType[BUFFER_SIZE];
        if(rTypeEnd){
            strncpy(rType, rTypeStart, rTypeEnd - rTypeStart);
            rType[rTypeEnd-rTypeStart] = '\0';
            printf("Skipping Unrecognized request type: %s\n\n", rType);
        }
        else{
            printf("Skipping Unrecognized request: %s\n\n", buffer);
        }
        close(client_soc);
        return;
    }
    else{
        printf("\nReceived request from client:\n%s", buffer);
    }

    // Extracting hostname from request ------------------------------------------------------
    char hostname[HOSTNAME_SIZE];
    char *hostname_start = strstr(buffer, "Host: ");
    char *hostname_end;
    if (!hostname_start){
        hostname_start = buffer;
        hostname_start += 4; // move past 'GET '
        hostname_start = strstr(hostname_start, "http://");
        if (hostname_start && hostname_start < strstr(buffer, "\r\n")){
            hostname_start += 7;
            hostname_end = strstr(hostname_start, "/");
        }
        else{
            hostname_start = strstr(hostname_start, "https://");
            if (hostname_start && hostname_start < strstr(buffer, "\r\n")){
                hostname_start += 8;
                hostname_end = strstr(hostname_start, "/");
            }
        }
        
        if(!hostname_end){
            hostname_end = strstr(hostname_start, " HTTP/");
        }
    }
    else{
        hostname_start += 6; // move past "Host: "
        hostname_end = strstr(hostname_start, "\r");
    }

    // Remove port number from hostname if present
    char *port_separator = strchr(hostname_start, ':');
    char* original_end = NULL;
    int port = 80; // Default HTTP port
    if (port_separator && port_separator < hostname_end && port_separator > hostname_start) {
        sscanf(port_separator + 1, "%d", &port);
        original_end = hostname_end;
        hostname_end = port_separator;
    }
    
    // Copy hostname value
    if(hostname_start && strlen(hostname_start) >= (hostname_end - hostname_start)){
        strncpy(hostname, hostname_start, hostname_end - hostname_start);
        hostname[hostname_end - hostname_start] = '\0';
        printf("Hostname: %s\n", hostname);
    }
    else{
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "Error: Request Hostname is Invalid: %s\n", hostname_start);
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(client_soc);
        return;
    }
    

    // Extract the length of the hostname
    size_t hostname_len;
    if(original_end){
        char original_hostname[HOSTNAME_SIZE];
        strncpy(original_hostname, hostname_start, original_end - hostname_start);
        original_hostname[original_end - hostname_start] = '\0';
        hostname_len = strlen(original_hostname);
    }
    else{
        hostname_len = strlen(hostname);
    }
    //------------------------------------------------------------------------------------------------------------

    // Extract the filepath
    char* path_start;
    path_start = strstr(buffer, hostname);
    path_start += hostname_len; // move past hostname
    if (!path_start) {
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "Error: Request Filepath Start not found!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(client_soc);
        return;
    }

    // check if filepath empty
    int emptypath = 0;
    char *path_end = strstr(path_start, " HTTP/");
    if (path_start == path_end || path_start[1] == path_end[0]){
        emptypath = 1;
    }
    else if (!path_end) {
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "Error: Request Filepath End Not Found!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(client_soc);
        return;
    }

    char path[997];
    if (emptypath){ // set path to '/' if empyty
        path[0] = '/';
        path[1] = '\0';
    }
    else{
        strncpy(path, path_start, path_end - path_start); // else copy path from request
        path[path_end - path_start] = '\0';
    }
    
    printf("Path: %s\n\n", path);

    char* additional_header_start;
    char* additional_header_end;
    char additional_header[ADDITIONAL_HEADER_SIZE];
    bzero(additional_header, ADDITIONAL_HEADER_SIZE);
    char additional_headers[ADDITIONAL_HEADER_LIST_SIZE];
    bzero(additional_headers, ADDITIONAL_HEADER_LIST_SIZE);
    int additional_headers_len = 0;

    char* request_end = strstr(buffer, "\r\n\r\n");
    if(!request_end){
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "Error: Terminating Empty Line Not Found!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    }
    else{ // Checking for additional headers other than the important 4
        additional_header_start = strstr(buffer, "\r\n");
        while(additional_header_start && additional_header_start < request_end && (additional_headers_len) < 2500){
            additional_header_start += 2; // moving past \r\n
            additional_header_end = strstr(additional_header_start, ":");
            strncpy(additional_header, additional_header_start, additional_header_end - additional_header_start);
            additional_header[additional_header_end - additional_header_start] = '\0';
            if (strcmp(additional_header, "Host") == 0){
                additional_header_start = strstr(additional_header_start, "\r\n");
                continue;
            }
            else if (strcmp(additional_header, "User-Agent") == 0){
                additional_header_start = strstr(additional_header_start, "\r\n");
                continue;
            }
            else if (strcmp(additional_header, "Connection") == 0){
                additional_header_start = strstr(additional_header_start, "\r\n");
                continue;
            }
            else if (strcmp(additional_header, "Proxy-Connection") == 0){
                additional_header_start = strstr(additional_header_start, "\r\n");
                continue;
            }
            else{
                additional_header_end = strstr(additional_header_start, "\r\n");// find end of additional header
                additional_header_end += 2; // move past \r\n
                strncpy(additional_header, additional_header_start, additional_header_end - additional_header_start); // copy full header
                additional_header[additional_header_end - additional_header_start] = '\0'; // set end of string
                if (additional_headers_len + strlen(additional_header) < ADDITIONAL_HEADER_LIST_SIZE){
                    strcat(additional_headers, additional_header);// add header to list of additional headers
                    additional_headers_len += strlen(additional_header); // add length to header length tracker
                    additional_header_start = (additional_header_end -= 2);
                }
                else{
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    fprintf(stderr, "Error: Additional Request Headers Overflow!\n");
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    close(client_soc);
                    return;
                }
            }
        }
        strcat(additional_headers, "\0");
        additional_headers_len++;
    }


    // Connect to server
    int server_soc = socket(AF_INET, SOCK_STREAM, 0);
    if (server_soc <= -1){
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "Error: Couldn't Open Socket!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(server_soc);
        close(client_soc);
        return;
    }

    // setting up server address
    struct sockaddr_in server_address;
    bzero((char *)&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;
    struct hostent *server = gethostbyname(hostname);
    if (!server){
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "Error: Couldn't Find Host!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(server_soc);
        close(client_soc);
        return;
    }
    bcopy((char *)server->h_addr_list[0], (char *)&server_address.sin_addr.s_addr, server->h_length);
    server_address.sin_port = htons(port);

    // connecting to server socket
    if (connect(server_soc, (struct sockaddr *)&server_address, sizeof(server_address)) <= -1){
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "Error: Couldn't Connect to Server!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(server_soc);
        close(client_soc);
        return;
    }

    char outgoingBuffer[BUFFER_SIZE];
    bzero(outgoingBuffer, BUFFER_SIZE);

    // Construct new HTTP request with User-Agent header
    if(additional_headers_len > 1){
        sprintf(outgoingBuffer, "GET %s HTTP/1.0\r\nHost: %s\r\n%s\r\nConnection: close\r\nProxy-Connection: close\r\n%s\r\n", path, hostname, user_agent_hdr, additional_headers);
    }
    else{
        sprintf(outgoingBuffer, "GET %s HTTP/1.0\r\nHost: %s\r\n%s\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", path, hostname, user_agent_hdr);
    }

    printf("Sending Request: %s", outgoingBuffer);

    // Forward the client's request to the server
    n = rio_writen(server_soc, outgoingBuffer, strlen(outgoingBuffer));
    if (n == -1 || n != strlen(outgoingBuffer)){
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "!!!RIO Error: Couldn't Write to Server Socket!!!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(server_soc);
        close(client_soc);
        return;
    }

    char incomingBuffer[BUFFER_SIZE];
    bzero(incomingBuffer, BUFFER_SIZE);
    rio_t server_rio;
    readBytes = 0;
    char* newAddressStart;
    char new_hostname[HOSTNAME_SIZE];
    bzero(new_hostname, HOSTNAME_SIZE);

    // Forward response to client
    printf("Server Response:\n");
    rio_readinitb(&server_rio, server_soc);
    while ((readBytes = rio_readnb(&server_rio, incomingBuffer, BUFFER_SIZE - 1)) >= 1) {
        printf("%s\n", incomingBuffer);
        char* httpCode301 = strstr(incomingBuffer, "301 Moved Permanently");
        char* httpCode302 = strstr(incomingBuffer, "302 Moved Temporarily");
        if (httpCode301){ // if location permanently moved
            newAddressStart = strstr(incomingBuffer, "The document has moved <a href=\"");
            if(newAddressStart){
                char*newAddressEnd = strstr(incomingBuffer, "\">here</a>.</p>");
                if(newAddressEnd){
                    hostname_start = strstr(newAddressStart, "://") + 3;
                    if(hostname_start){
                        hostname_end = strstr(hostname_start, "/");
                        char* httpsChecker = strstr(newAddressStart, "https");
                        if(hostname_end){
                            bzero(new_hostname, HOSTNAME_SIZE);
                            strncpy(new_hostname, hostname_start, hostname_end - hostname_start);
                            new_hostname[hostname_end - hostname_start] = '\0';
                            printf("New Location Hostname: %s\n", new_hostname);
                            if (httpsChecker){
                                port = 443;
                            }
                            path_start = hostname_end;
                            path_end = newAddressEnd;
                            strncpy(path, path_start, path_end - path_start);
                            path[path_end - path_start] = '\0';
                            bzero(outgoingBuffer, BUFFER_SIZE);
                            // Modify HTTP request with new location
                            if(additional_headers_len > 1){
                                sprintf(outgoingBuffer, "GET %s HTTP/1.0\r\nHost: %s\r\n%s\r\nConnection: close\r\nProxy-Connection: close\r\n%s\r\n", path, new_hostname, user_agent_hdr, additional_headers);
                            }
                            else{
                                sprintf(outgoingBuffer, "GET %s HTTP/1.0\r\nHost: %s\r\n%s\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", path, new_hostname, user_agent_hdr);
                            }
                            printf("\nModified Request For 301: \n%s\n", outgoingBuffer);
                            close(server_soc);
                            server_soc = socket(AF_INET, SOCK_STREAM, 0);
                            if (server_soc <= -1){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "Error: Couldn't Open NEW Socket!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            bzero((char *)&server_address, sizeof(server_address));
                            server_address.sin_family = AF_INET;
                            server = gethostbyname(new_hostname);
                            if (!server){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "Error: Couldn't Find NEW Host!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            bcopy((char *)server->h_addr_list[0], (char *)&server_address.sin_addr.s_addr, server->h_length);
                            server_address.sin_port = htons(port);
                            if (connect(server_soc, (struct sockaddr *)&server_address, sizeof(server_address)) <= -1){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "Error: Couldn't Connect to NEW Server!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            // Forward the client's modified request to the server
                            n = rio_writen(server_soc, outgoingBuffer, strlen(outgoingBuffer));
                            if (n == -1 || n != strlen(outgoingBuffer)){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "!!!RIO Error: Couldn't Write to NEW Server Socket!!!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            bzero(incomingBuffer, BUFFER_SIZE);
                            rio_readinitb(&server_rio, server_soc);
                            printf("Server Response:\n");
                        }
                        else{
                            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                            fprintf(stderr, "Error: Couldn't Find end of NEW hostname for moved object!\n");
                            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                            close(server_soc);
                            close(client_soc);
                            return;
                        }
                    }
                    else{
                        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                        fprintf(stderr, "Error: Couldn't Find start of NEW hostname for moved object!\n");
                        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                        close(server_soc);
                        close(client_soc);
                        return;
                    }
                }
                else{
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    fprintf(stderr, "Error: Couldn't Find end of NEW address for moved object!\n");
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    close(server_soc);
                    close(client_soc);
                    return;
                }
            }
            else{
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("Error: 301 Recieved but new address not found!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                n = rio_writen(client_soc, incomingBuffer, readBytes);
                printf("%s\n", incomingBuffer);
                if (n == -1 || n != readBytes){
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    fprintf(stderr, "!!!RIO Error: Couldn't Write to Client Socket!!!\n");
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    close(server_soc);
                    close(client_soc);
                    return;
                }
                bzero(incomingBuffer, BUFFER_SIZE);
            }
        }
        else if (httpCode302){ // if location temporarily moved
            char* newAddressStart = strstr(incomingBuffer, "Location: ");
            if(newAddressStart){
                char*newAddressEnd = strstr(newAddressStart, "\r\n");
                if(newAddressEnd){
                    hostname_start = strstr(newAddressStart, "://") + 3;
                    if(hostname_start){
                        hostname_end = strstr(hostname_start, "/");
                        char* httpsChecker = strstr(newAddressStart, "https");
                        if(hostname_end){
                            bzero(new_hostname, HOSTNAME_SIZE);
                            strncpy(new_hostname, hostname_start, hostname_end - hostname_start);
                            new_hostname[hostname_end - hostname_start] = '\0';
                            printf("New Location Hostname: %s\n", new_hostname);
                            if (httpsChecker){
                                port = 443;
                            }
                            path_start = hostname_end;
                            path_end = newAddressEnd;
                            strncpy(path, path_start, path_end - path_start);
                            path[path_end - path_start] = '\0';
                            bzero(outgoingBuffer, BUFFER_SIZE);
                            // Modify HTTP request with new location
                            if(additional_headers_len > 1){
                                sprintf(outgoingBuffer, "GET %s HTTP/1.0\r\nHost: %s\r\n%s\r\nConnection: close\r\nProxy-Connection: close\r\n%s\r\n", path, new_hostname, user_agent_hdr, additional_headers);
                            }
                            else{
                                sprintf(outgoingBuffer, "GET %s HTTP/1.0\r\nHost: %s\r\n%s\r\nConnection: close\r\nProxy-Connection: close\r\n\r\n", path, new_hostname, user_agent_hdr);
                            }
                            printf("\nModified Request For 302: \n%s\n", outgoingBuffer);
                            close(server_soc);
                            server_soc = socket(AF_INET, SOCK_STREAM, 0);
                            if (server_soc <= -1){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "Error: Couldn't Open NEW Socket!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            bzero((char *)&server_address, sizeof(server_address));
                            server_address.sin_family = AF_INET;
                            server = gethostbyname(new_hostname);
                            if (!server){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "Error: Couldn't Find NEW Host!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            bcopy((char *)server->h_addr_list[0], (char *)&server_address.sin_addr.s_addr, server->h_length);
                            server_address.sin_port = htons(port);
                            if (connect(server_soc, (struct sockaddr *)&server_address, sizeof(server_address)) <= -1){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "Error: Couldn't Connect to NEW Server!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            // Forward the client's modified request to the server
                            n = rio_writen(server_soc, outgoingBuffer, strlen(outgoingBuffer));
                            if (n == -1 || n != strlen(outgoingBuffer)){
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                fprintf(stderr, "!!!RIO Error: Couldn't Write to NEW Server Socket!!!\n");
                                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                                close(server_soc);
                                close(client_soc);
                                return;
                            }
                            bzero(incomingBuffer, BUFFER_SIZE);
                            rio_readinitb(&server_rio, server_soc);
                            printf("Server Response:\n");
                        }
                        else{
                            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                            fprintf(stderr, "Error: Couldn't Find end of NEW hostname for moved object!\n");
                            printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                            close(server_soc);
                            close(client_soc);
                            return;
                        }
                    }
                    else{
                        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                        fprintf(stderr, "Error: Couldn't Find start of NEW hostname for moved object!\n");
                        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                        close(server_soc);
                        close(client_soc);
                        return;
                    }
                }
                else{
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    fprintf(stderr, "Error: Couldn't Find end of NEW address for moved object!\n");
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    close(server_soc);
                    close(client_soc);
                    return;
                }
            }
            else{
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                printf("Error: 302 Recieved but new address not found!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                n = rio_writen(client_soc, incomingBuffer, readBytes);
                printf("%s\n", incomingBuffer);
                if (n == -1 || n != readBytes){
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    fprintf(stderr, "!!!RIO Error: Couldn't Write to Client Socket!!!\n");
                    printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                    close(server_soc);
                    close(client_soc);
                    return;
                }
                bzero(incomingBuffer, BUFFER_SIZE);
            }
        }
        else{
            n = rio_writen(client_soc, incomingBuffer, readBytes);
            if (n == -1 || n != readBytes){
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                fprintf(stderr, "!!!RIO Error: Couldn't Write to Client Socket!!!\n");
                printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
                close(server_soc);
                close(client_soc);
                return;
            }
            bzero(incomingBuffer, BUFFER_SIZE);
        }
    }
    if(n == 0){
        printf("No Response From Server\n");
    }
    else if (n <= -1){
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        fprintf(stderr, "!!!RIO Error: Couldn't Read from Server Socket!!!\n");
        printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
        close(server_soc);
        close(client_soc);
        return;
    }
    printf("----------------------------------------------------------------------\n\n");

    // Close sockets
    close(server_soc);
    close(client_soc);
}

volatile sig_atomic_t proxy_running = 1;

void sigint_handler(int signum) {
    proxy_running = 0;  // Set the flag to false to stop the proxy
}

int main(int argc, char **argv)
{
    // Registering signal handler to stop proxy loop
    signal(SIGINT, sigint_handler);

    // Checking that commandline arguments are correct
    if (argc != 2) {
        fprintf(stderr, "Error: Include Port in commandline!\n");
        exit(1);
    }
    int port = atoi(argv[1]);

    // personal port for debugging without arguments
    //int port = 40064;

    // Defining sockets and addresses
    int proxy_soc, client_soc, ipv6 = 0;
    struct sockaddr_in proxy_address, client_address;
    socklen_t client_len = sizeof(client_address);

    // Creating proxy socket
    proxy_soc = socket(AF_INET, SOCK_STREAM, 0); // Include issues wouldn't recognize addrinfo struct definition
    if (proxy_soc <= -1){
        proxy_soc = socket(AF_INET6, SOCK_STREAM, 0);
        ipv6 = 1;
        if (proxy_soc <= -1){
            fprintf(stderr, "Error: Couldn't Open Socket!\n");
            close(proxy_soc);
            exit(1);
        }
    }

    // Allocating memory for proxy address
    bzero((char *)&proxy_address, sizeof(proxy_address));

    // Initializing proxy address
    if(ipv6 == 0){
        proxy_address.sin_family = AF_INET; // setting to ipv4
    }
    else{
        proxy_address.sin_family = AF_INET6; // setting to ipv6
    }
    proxy_address.sin_addr.s_addr = INADDR_ANY; // accepting any connections on the designated port
    proxy_address.sin_port = htons(port); // setting port to input val

    // Binding proxy socket
    if (bind(proxy_soc, (struct sockaddr *)&proxy_address, sizeof(proxy_address)) <= -1){
        fprintf(stderr, "Error: Couldn't Bind Socket!\n");
        close(proxy_soc);
        exit(1);
    }

    // Getting proxy socket to listen
    listen(proxy_soc, LISTENQ);
    printf("Proxy listening on port: %d\n", port);


    while (proxy_running == 1) {
        bzero((char *)&client_address, sizeof(client_address));
        // Accepting connection from client
        client_soc = accept(proxy_soc, (struct sockaddr *)&client_address, &client_len); // waiting for connection to accept then accepting
        if (client_soc <= -1){
            fprintf(stderr, "Error: Couldn't Accept Socket!\n");
            close(client_soc);
            continue;
        }

        int client_port = ntohs(client_address.sin_port);
        char* client_addr_str = inet_ntoa(client_address.sin_addr);
        printf("Accepted connection from %s:%d\n", client_addr_str, client_port);

        // Handling the request
        proxy(client_soc);
    }

    close(proxy_soc);

    return 0;
}