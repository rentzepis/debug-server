/*
 * proxy.c
 *
 * visited url log to file log.list
 * block url base on entry from file block.list
 */
#include <stdio.h>
#include "../csapp.h"

#define FILTER_FILE "block.txt"
#define LOG_FILE "proxy.log"

/* 
 * if uri is full absolute path "http://abc.com/path" that is okay to return 1.
 * if uri is absolute path "abc.com/path" but don't have scheme "http://"
 * that is risky, return 0.
 * if uri is relative path "/path/to/resource", wrong format, return -1.
 * 
 * parse the full path uri http://example.com/index.html
 * or http://example.com:8080/index.html to get: 
 * 1. hostname (e.g. www.google.com)
 * 2. port (default: 80)
 * 3. path (/index.html)
 */
int parse_uri(char *uri, char *host, char *path, int *port) {
    *port = 80;   // default port is 80
    if (uri[0] == '/') 
        return -1;
    char *pos = strstr(uri, "//");
    if (!pos)
        return 0;
    pos += 2;   // pos = example.com:8080/index.html
    char *colon = strstr(pos, ":");
    char *slash = strstr(pos, "/");

    if (colon) {
        *colon = '\0';
        sscanf(pos, "%s", host);
        sscanf(colon + 1, "%d%s", port, path); //path = "/index.html"
    } else if (slash) { //pos = "example.com/index.html"
        *slash = '\0';
        strcpy(host, pos);
        *slash = '/';
        strcpy(path, slash);
    } else {  // pos = example.com
        strcpy(host, pos);
        strcpy(path, "/");
    }
    return 1;
}

/*
 * check uri contained any piece of blocklist or not
 */
int is_blocked(const char *uri) {
    FILE *fp = fopen(FILTER_FILE, "r");
    if (!fp) return 0;
    char line[MAXBUF];
    while (fgets(line, MAXBUF, fp)) {
        if (strstr(uri, strtok(line, "\n "))) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

void log_request(const char *uri) {
    FILE *log = fopen(LOG_FILE, "a");
    if (log) {
        time_t now = time(NULL);
        fprintf(log, "%s %s\n", ctime(&now), uri);
        fclose(log);
    } else {
        fprintf(stderr, "The log file can not be open\n");
    }
}

/*
 * 1. Read request line using Rio_readlineb
 * and extract method, uri, and version from it.
 * 2. block uri if it in the filter list
 * 3. sent relative path format request line and headers to the server
 * 4. logging the uri toa proxy.log file
 */
void handle_request(int connfd) {
    rio_t client_rio, server_rio;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];

    Rio_readinitb(&server_rio, connfd);
    // read request line and header
    if (!Rio_readlineb(&server_rio, buf, MAXLINE)) {
        Close(connfd);
        return;
    }
    sscanf(buf, "%s %s %s", method, uri, version);

    // parse uri
    char host[MAXLINE], path[MAXLINE];
    int port;
    int parse = parse_uri(uri, host, path, &port);
    if (parse == 0) {
        fprintf(stderr, "This request's uri has not scheme 'http://'\n");
        Close(connfd);
        return;
    } else if (parse == -1) {
        fprintf(stderr, "This request'uri is a relative path, it should be full path\n");
        Close(connfd);
        return;
    }

    // check block list
    if (is_blocked(uri)) {
        char *err = "HTTP/1.0 403 Forbidden\r\n\r\nBlocked by proxy\n";
        Rio_writen(connfd, err, strlen(err));
        Close(connfd);
        return;
    }

    log_request(uri);

    // forward request to server
    char port_char[10];
    sprintf(port_char, "%d", port);
    int clientfd = Open_clientfd(host, port_char);
    if (clientfd < 0) {
        Close(connfd);
        return;
    } 

    // send request line to the server
    sprintf(buf, "%s %s %s\r\n", method, path, version);
    Rio_writen(clientfd, buf, strlen(buf));

    //forward request headers to the server
    while (Rio_readlineb(&server_rio, buf, MAXLINE) > 0) {
        if (strcmp(buf, "\r\n") == 0) break;
        if (strncasecmp(buf, "Connection:", 11) == 0 ||
            strncasecmp(buf, "Proxy-connection:", 17) == 0 ||
            strncasecmp(buf, "Keep-Alive:", 11) == 0) {
                continue;
        }
        Rio_writen(clientfd, buf, strlen(buf));
    }

    //add our own connection headers
    sprintf(buf, "Connection: close\r\nProxy-Connection: close\r\n\r\n");
    Rio_writen(clientfd, buf, strlen(buf));

    // read response from server and forward it back to the browser
    Rio_readinitb(&client_rio, clientfd);
    ssize_t n;
    while ((n = Rio_readnb(&client_rio, buf, MAXLINE)) > 0) {
        Rio_writen(connfd, buf, n);
    }
    Close(clientfd);
    Close(connfd);
}

int main(int argc, char **argv) {
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    Signal(SIGPIPE, SIG_IGN);
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        fprintf(stderr, "use default port 5050\n");
        listenfd = Open_listenfd("5050");
    } else {
        listenfd = Open_listenfd(argv[1]);
    }

    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        handle_request(connfd);
    }
}