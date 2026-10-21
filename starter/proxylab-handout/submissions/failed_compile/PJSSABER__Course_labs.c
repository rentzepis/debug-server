#pragma once
#include <stdio.h>
#include "csapp.h"
#include <stdbool.h>
#include "cache.c"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


/*
    basic functions:
    1. listen for incoming connections on a port 
    2. Once a connection is established, your proxy should read the entirety of the request
from the client and parse the request
        if the client has sent a valid HTTP request;
            establish its own connection to the appropriate web server then request the object the client specified. 
            important: forward HTTP/1.0 always
            Host header
            Connection: close
            Proxy-Connection: close 
            user_agent_hdr
        else 
            error and return
    3. your proxy should read the server’s response and forward it to the client

    4. should never prematurely abort due to a malformed request
    5. Your proxy must properly function whether or not the port number is included in the URL(by default is port 80)
    6(done). Your proxy should accept a command line argument specifying the listening port number for your proxy.
*/

#define clr(x) memset(x, 0, sizeof x)
typedef struct sockaddr_storage sockaddr_storage;

void *doit(void *fd);
int debug = 1;
cache *ptr;

void yiyu_debug(char *s)
{
    if (debug) {
        printf("%s", s);
    }
}

int main(int argc, char **argv)
{
    int listenfd, connectfd;
    sockaddr_storage clientaddr; socklen_t clientlen;
    pthread_t tid;
    // variable declare
    if (argc != 2) {
        unix_error("usage: just specify a port\n");
	    exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    ptr = init();
    while (1) {
        connectfd = Accept(listenfd, &clientaddr, &clientlen);
        Pthread_create(&tid, NULL, doit, (void*)connectfd);
        // doit(connectfd);
        Pthread_detach(tid);
    }

    printf("%s", user_agent_hdr);
    return 0;
}

void parse_uri(char *uri, char *hostname, char *port, char *file)
{
    char *colon = strstr(uri, ":");
    if (colon) {
        colon++;
    }
    colon = strstr(colon, ":"); // http://www.cmu.edu:8080/hub/index.html  http://www.cmu.edu:8080 pass first
    if (colon) {   
        colon++; 
        while (*colon != '\0' && *colon != '/') {
            *port = *colon;
            colon++; 
            port++;
        }
    } else { // no port
        strcpy(port, "80");
    }

    uint32_t uri_len = strlen(uri);
    int flag = 0;
    for (int i = 0; i < uri_len; i++) {
        if (flag == 2) {
            if (uri[i] == '/' || uri[i] == ':') {
                break;
            }
            *hostname = uri[i];
            hostname++;
        } else {
            if (uri[i] == '/') {
                flag += 1;
            }
        }
    }

    flag = 0; int file_len = 0;
    for (int i = 0; i < uri_len; i++) {
        if (uri[i] == '/') {
            flag += 1;
        } 
        
        if (flag == 3) {
            file[file_len++] = uri[i];
        }
    }

    if (file_len == 0) {
        strcpy(file, "/home.html");
    }
}

void read_request_header(rio_t *rp, char *request_header, char* hostname, char *port) 
{
    char buf[MAXLINE], key[MAXLINE], value[MAXLINE]; clr(buf);
    bool host_specify = false; // if should use hostname
    int req_pos = 0;

    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
        if (!host_specify) {
            sscanf(buf, "%s %s", key, value);
            if (!strcmp(key, "Host:")) { // if have Host-header   sometime " Host: localhost:32885 "
                host_specify = true;
                char *colon = strstr(value, ":");
                char *tmp = hostname; // overwrite hostname
                for (char *itr = value; itr != colon; itr++, tmp++) {
                    *tmp = *itr;
                }
                *tmp = '\0';

                if (colon) {    // if port specified, overwrite
                    colon++; 
                    while (*colon != '\0' && *colon != '/') {  // !! can not use !colon
                        *port = *colon;
                        colon++; 
                        port++;
                    }
                    *port = '\0';
                } 
            }
        }
        req_pos += sprintf(request_header + req_pos, buf);
        Rio_readlineb(rp, buf, MAXLINE);
        printf("%s", buf);
    }
    
    // add optional here
    req_pos += sprintf(request_header + req_pos, "Connection: close\r\n");
    req_pos += sprintf(request_header + req_pos, "Proxy-Connection: close\r\n");
    req_pos += sprintf(request_header + req_pos, user_agent_hdr);
    if (!host_specify) {  // if host not specify, parse from url
        req_pos += sprintf(request_header + req_pos, "Host: ");
        req_pos += sprintf(request_header + req_pos, hostname);
        req_pos += sprintf(request_header + req_pos, "\r\n");
    }
    req_pos += sprintf(request_header + req_pos, "\r\n");
    return;
}

void forward_msg(char *hostname, char *port, char *send_msg, int clientfd, char *cache_key)
{
    int proxyfd, n;
    rio_t rio;
    char buf[MAXLINE]; clr(buf);
    
    char *cache_line = check_cache(cache_key, ptr);
    if (cache_line != NULL) {
        Rio_writen(clientfd, cache_line, strlen(cache_line));
        return;
    }

    yiyu_debug(send_msg);
    proxyfd = Open_clientfd(hostname, port);
    Rio_readinitb(&rio, proxyfd);
    Rio_writen(proxyfd, send_msg, strlen(send_msg));

    bool to_cache = true;
    char *cache_val = (char*)calloc(MAX_OBJECT_SIZE, 8);  // to store cache val
    int cur_size = 0;

    while ( (n = Rio_readlineb(&rio, buf, MAXLINE)) != 0 ) {          //line:netp:readhdrs:checkterm
        printf("%s", buf);
        if (to_cache) {
            if (n + cur_size >= MAX_OBJECT_SIZE) {
                to_cache = false;
            } else {
                cur_size += n;
                strcat(cache_val, buf);
            }
        }
        Rio_writen(clientfd, buf, n);
    }
    //write to clientfd
    Close(proxyfd);
    if (to_cache) {
        push_cache(cache_key, cache_val, ptr);
    }
    free(cache_val);
}

void *doit(void *clientfd_t) 
{
    int clientfd = (int) clientfd_t;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], hostname[MAXLINE],//if not specified in header, parse from uri 
    port[MAXLINE], request_header[MAXLINE];//if not specified in header, 80 
    char send_msg[MAXLINE]; int send_pos = 0;
    char file[MAXLINE];
    clr(buf); clr(method); clr(uri); clr(version); clr(hostname); clr(port); clr(request_header); clr(send_msg); clr(file);
    // important！！！为什么要清除？ bug; 先跑 csapp.c 后跑 tiny的时候， stack复用后， file会是 tinyp.c

    // for cache
    char cache_key[MAXLINE]; 
    clr(cache_key); 
    int cache_pos = 0;

    rio_t rio;

    Rio_readinitb(&rio, clientfd);
    // 1.read request line, check method, uri, version, check if valid
    if (!Rio_readlineb(&rio, buf, MAXLINE)) {
        return NULL;
    }
    yiyu_debug(buf);
    sscanf(buf, "%s %s %s", method, uri, version);

    if (strcasecmp(method, "GET")) {
        printf("Not support HTTP type, support only GET!");
        return NULL;
    } else {
        send_pos += sprintf(send_pos + send_msg, method);
        send_pos += sprintf(send_pos + send_msg, " ");
    }
   

    // check port
    parse_uri(uri, hostname, port, file);
    send_pos += sprintf(send_msg + send_pos, file);
    send_pos += sprintf(send_msg + send_pos, " HTTP/1.0 \r\n");   // finish first

    // 2.read request-header, check if host exist, fullfill request_header, also append what's needed(version, suffix)
    read_request_header(&rio, request_header, hostname, port);
    // 3.combine the send_msg, 
    send_pos += sprintf(send_msg + send_pos, request_header);
    
    // update cache_key
    cache_pos += sprintf(cache_key + cache_pos, hostname);
    cache_pos += sprintf(cache_key + cache_pos, port);
    cache_pos += sprintf(cache_key + cache_pos, file);

    // 4.open connect to the server 
    /*
        Open connection to server at <hostname, port>
        proxyfd = open_clientfd(hostname, port)
        // 5.write to the server and get respond
        // 7.write to clientfd
        // 8.close proxyfd
    */

    forward_msg(hostname, port, send_msg, clientfd, cache_key);
    Close(clientfd);
}

