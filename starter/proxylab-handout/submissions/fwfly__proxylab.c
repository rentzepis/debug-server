#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void doit(int fd);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
    printf("%s", user_agent_hdr);

    // copied from tiny.c.
    int listenfd, connfd;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check command line args */
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
        Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
                    port, MAXLINE, 0);
        printf("Accepted connection from (%s, %s)\n", hostname, port);
        doit(connfd);                                             //line:netp:tiny:doit
        Close(connfd);                                            //line:netp:tiny:close
    }

    return 0;
}
void doit(int fd)
{
    rio_t rio;
    char buf[MAXLINE];
    Rio_readinitb(&rio, fd);
    if(!Rio_readlineb(&rio, buf, MAXLINE))
        return;
    printf("%s", buf);
    // To do : Parsing method

    while(strcmp(buf, "\r\n")){
        if(!Rio_readlineb(&rio, buf, MAXLINE))
            return;
        printf("%s", buf);

        // Open cliet fd for tiny
        // send to tiny
        // accept reply and print
    }
}
