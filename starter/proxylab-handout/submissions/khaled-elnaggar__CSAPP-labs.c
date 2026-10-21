#include <stdio.h>
#include <stdio.h>
#include <stdbool.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define URL_LINE 500
#define RESOURCE_LINE 200

typedef struct {
    char host[RESOURCE_LINE];
    char port[RESOURCE_LINE];
    char resource[RESOURCE_LINE];
    char headers[5*MAXLINE];
} request_params;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static char bad_request[MAXLINE] = "\nHTTP/1.0 400 Bad Request\r\n\r\n400 - Bad Request\r\nPlease send request in the format GET www.example.com HTTP/1.0\r\n";

void validate_port(char *port, char *parsed_port);
void echo(int connfd);
void *thread(void *vargp);
void parse_url(char URL[URL_LINE], request_params *params);
void forward_request(bool host_included, request_params *params, char response[MAX_OBJECT_SIZE], int *content_length);
void reset_request(char *headers, bool *waiting);
void return_bad_request(int connfd);
void prepare_request(char request[10*MAXLINE], bool host_included, request_params *params);

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("ERROR: No port number provided\nRun program as %s <portnumber>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    char listening_port[6];
    validate_port(argv[1], listening_port);
    
    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid; 
    
    listenfd = Open_listenfd(argv[1]);
    printf("Proxy server listening on port %s\n", listening_port);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfdp = Malloc(sizeof(int));
        *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen);
        Pthread_create(&tid, NULL, thread, connfdp);
    }

    return 0;
}
void *thread(void *vargp) {  
    int connfd = *((int *) vargp);
    Pthread_detach(pthread_self());
    printf("Connected to client %d\n", connfd);
    echo(connfd);
    Close(connfd);
    printf("Closed connection to client %d\n", connfd);
    return NULL;
}

void echo(int connfd) {
    int n, content_length = 0;
    char buf[MAXLINE]; 
    rio_t rio;

    char method[RESOURCE_LINE], URL[URL_LINE], http_version[RESOURCE_LINE];
    request_params params;
    char response[MAX_OBJECT_SIZE]; 
    bool waiting_request_line = true, host_included = false;
    Rio_readinitb(&rio, connfd);

    while((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        printf("Received message %s", buf);
        if (strncmp(buf, "\r\n", 2) == 0) {
            if (!waiting_request_line) {
                printf("Getting you %s %s %s\n", method, params.host, params.resource);
                forward_request(host_included, &params, response, &content_length);
                Rio_writen(connfd, response, content_length);
                reset_request(params.headers, &waiting_request_line);
                continue;
            } else {
                continue;
            }
        } else if (waiting_request_line) {
            int read = sscanf(buf, "%s %s %s", method, URL, http_version);

            if (read == 3) {
                parse_url(URL, &params);
            } else {
                return_bad_request(connfd);
                reset_request(params.headers, &waiting_request_line);
                continue;
            }

            if (strncasecmp(method, "GET", 3) != 0) {
                return_bad_request(connfd);
                reset_request(params.headers, &waiting_request_line);
                continue;
            }
            printf("User %d requesting %s resource: %s from host: %s:%s \n", connfd, method, params.resource, params.host, params.port);
            waiting_request_line = false;
        } else {

            if (strncmp(buf, "Host", 4) == 0) {
                host_included = true;
            }
            strcat(params.headers, buf);
        }
    }
}

void forward_request(bool host_included, request_params *params, char response[MAX_OBJECT_SIZE], int *content_length) {
    char request[10*MAXLINE];
    int serverfd, n = 0;
    rio_t rio;
    prepare_request(request, host_included, params);
    serverfd = Open_clientfd(params->host, params->port);
    printf("HEEEY successfully connected to server on %d\n\n", serverfd);

    Rio_readinitb(&rio, serverfd);
    Rio_writen(serverfd, request, strlen(request));

    if((n = Rio_readn(serverfd, response, 10*MAXLINE)) == 0) {
        strcpy(response, "ERROR! no response was returned\n\n");
    }

    *content_length = n;
    close(serverfd);
}

void prepare_request(char request[10*MAXLINE], bool host_included, request_params *params) {
    sprintf(request, "GET %s HTTP/1.0\r\nConnection: close\r\nProxy-Connection: close\r\n%s", params->resource, user_agent_hdr);
    if (!host_included) {
        char tmp[MAXLINE];
        sprintf(tmp, "Host: %s\r\n", params->host);
        strcat(request, tmp);
    }
    strcat(request, params->headers);
    strcat(request, "\r\n");
    printf("\n");
    printf("going to send \n%s\n", request);
    return;
}

void return_bad_request(int connfd) {
    Rio_writen(connfd, bad_request, strlen(bad_request));
}

void reset_request(char *headers, bool *waiting) {
    headers[0] = '\0';
    *waiting = true;
}
void parse_url(char URL[URL_LINE], request_params *params) {
    if (strncasecmp(URL, "http://", 7) == 0) {
        URL += 7;
    }
    char *pp = strstr(URL, ":");
    char *rp = strstr(URL, "/");

    if (pp == NULL) {
        strcpy(params->port, "80");
        if (rp == NULL) {
            strcpy(params->resource, "/");
            strncpy(params->host, URL, strlen(URL));
        } else {
            strcpy(params->resource, rp);
            strncpy(params->host, URL, rp - URL);
            params->host[rp - URL + 1] = '\0';
        }
    } else {
        strncpy(params->host, URL, pp - URL);
        params->host[pp - URL + 1] = '\0';
        if (rp == NULL) {
            strcpy(params->resource, "/");
            strncpy(params->port, pp + 1, strlen(URL));
        } else {
            strcpy(params->resource, rp);
            strncpy(params->port, pp + 1, rp - pp - 1);
            printf("PP = %s\n", params->port);
        }
    }
}

void validate_port(char *port, char *parsed_port) {
    int listening_port = atoi(port);
    if (listening_port <= 1024 || listening_port >= 65536) {
        printf("ERROR: Please provide a numeric port number between 1025 and 65535\n");
        exit(EXIT_FAILURE);
    }
    sprintf(parsed_port, "%d", listening_port);
}
