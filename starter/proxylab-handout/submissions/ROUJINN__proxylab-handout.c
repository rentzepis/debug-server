/* proxy.c - a simple proxy between client and server.
 * 
 * 罗骏 2200011351@stu.pku.edu.cn
 */

#include <stdio.h>
#include "csapp.h"
#include "cache.h"

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* reap all children */
void sigchld_handler(int sig) {
    int bkp_errno = errno;
    while(waitpid(-1, NULL, WNOHANG)>0);
    errno=bkp_errno;
}

/* give a string url_in, parse_url split it to find char, port, and uri. 
 * return -1 if url_in isn't valid, otherwise return 0.
 */
int parse_url(char *url_in, char *host, char *port, char *uri) {
    char url[MAXLINE];
    strcpy(url,url_in);
    char *p1 = strstr(url,"//");
    if (p1 == NULL) {
        return -1;
    }
    char *p2 = index(p1+2,':');
    /* if there's no port, set it to 80 */
    if (p2 == NULL) {
        port = "80";
        char *p3 = index(p1+2,'/');
        *p3 = '\0';
        strcpy(host,p1+2);
        strcat(uri,"/");
        strcat(uri,p3+1);
        return 0;        
    }
    else {
        int port_number;
        sscanf(p2+1,"%d%s",&port_number,uri);
        sprintf(port, "%d", port_number);
        *p2 = '\0';
        strcpy(host,p1+2);
        return 0;
    }
}

/* use the information from client to build a header to send to server.
 */
void build_header(rio_t *client_rio, char *header, char *host, char *port) {
    char name[MAXLINE],data[MAXLINE],client_buf[MAXLINE];
    int has_host = 0;
    int n = 1;
    
    while(n > 0) {
        n = Rio_readlineb(client_rio, client_buf, MAXLINE);
        
        if (!strcasecmp(client_buf, "\r\n")) {
            break;
        }
        
        sscanf(client_buf,"%s %s",name,data);
        
        if (strcasecmp(name,"Host:") == 0) {
            strcat(header, client_buf);
            has_host = 1;
            continue;
        }
        if (strcasecmp(name,"User-Agent:") && strcasecmp(name,"Connection:") 
        && strcasecmp(name,"Proxy-Connection:")) {
            strcat(header, client_buf);
        }
    }
    if (has_host == 0) {
        sprintf(header, "%sHost: %s:%s\r\n", header, host, port);
    }
    strcat(header, user_agent_hdr);
    strcat(header, "Connection: close\r\n");
    strcat(header, "Proxy-Connection: close\r\n");
    strcat(header,"\r\n");
    return;
}

void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) {
    char buf[MAXLINE], body[MAXBUF];

    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wformat-overflow" 
        /* Build the HTTP response body */
        sprintf(body, "<html><title>Tiny Error</title>");
        sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
        sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
        sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
        sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);
    #pragma GCC diagnostic pop
        /* Print the HTTP response */
        sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
        rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Content-type: text/html\r\n");
        rio_writen(fd, buf, strlen(buf));
        sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
        rio_writen(fd, buf, strlen(buf));
        rio_writen(fd, body, strlen(body));
}

/* After connecting with client, play as a client to connect to server */
void doit(int fd) {

    int server_fd;
    rio_t client_rio, server_rio;
    char client_buf[MAXLINE], server_buf[MAXLINE];
    char method[MAXLINE], url[MAXLINE], version[MAXLINE]; 
    char host[MAXLINE], port[MAXLINE], uri[MAXLINE];
    char header[10*MAXLINE]; 
    char object[MAX_OBJECT_SIZE];  

    Rio_readinitb(&client_rio, fd);
    Rio_readlineb(&client_rio, client_buf, MAXLINE);

    sscanf(client_buf, "%s %s %s", method, url, version);

    if (strcasecmp(method, "GET")) { 
        clienterror(fd, method, "501", "Not Implemented",
                    "Tiny does not implement this method");
        return;
    }

    int target_line = read_cache(url,fd);

    /* hit cache, so we don't need to connect to server */
    if (target_line != -1) {
        return; 
    }

    parse_url(url,host,port,uri);

    sprintf(header, "%s %s HTTP/1.0\r\n",method,uri);
    build_header(&client_rio, header, host, port);

    server_fd = open_clientfd(host, port);
    if (server_fd < 0) {
        printf("open_clientfd:wrong ,header: %s\n",header);
        fflush(stdout);
        return;
    }
    
    Rio_readinitb(&server_rio, server_fd);
    if (rio_writen(server_fd, header, strlen(header))!= strlen(header)) {
        printf("rio_writen:wrong ,header: %s\n",header);        
    }

    int size;
    int need_write = 1;
    int n = Rio_readnb(&server_rio, server_buf, MAXLINE);
    while (n > 0) {
        if (rio_writen(fd, server_buf, n) != n) {
            printf("rio_writen:wrong ,header: %s\n",header);
            break;
        }
        if (size + n > MAX_OBJECT_SIZE) {
            need_write = 0;
        }
        if (need_write) {
            memcpy(object+size,server_buf,n);
        }
        size += n;
        n = Rio_readnb(&server_rio, server_buf, MAXLINE);
    }
    if (need_write) {
        write_cache(object, size, url);
    }
    if (close(server_fd) < 0) {
        printf("close:wrong, header: %s\n",header);
    }
}

/* new thread routine to connect to server */
void *thread(void *vargp) {
    int connfd = *((int *)vargp);
    Pthread_detach(pthread_self());
    Free(vargp);
    doit(connfd);
    Close(connfd);
    return NULL;
}

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, sigchld_handler);

    int listenfd, *connfdp;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    pthread_t tid;

    if (argc != 2) {
	    fprintf(stderr, "usage: %s <port>\n", argv[0]);
	    exit(1);
    }

    listenfd = open_listenfd(argv[1]);
    init_cache();

    while (1) {
        clientlen = sizeof(clientaddr);
        connfdp = Malloc(sizeof(int));
        *connfdp = accept(listenfd, (SA *)&clientaddr, &clientlen); 
        Pthread_create(&tid, NULL, thread, connfdp);                                     
    }

    free_cache();
}

