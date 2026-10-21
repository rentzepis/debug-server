#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include "csapp.h"

#include<errno.h>
#include<fcntl.h>
#include<stdlib.h>
#include<sys/epoll.h>
#include<sys/socket.h>

#define MAX_OBJECT_SIZE 102400

#define MAX_OBJECT_SIZE 102400


void logURI(char* uri);
void sigint_handler();


FILE *fp;
char* temp;
char* input;



/* You won't lose style points for including this long line in your code */
//static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";


void sigint_handler() {
    fclose(fp);
    free(input);
    free(temp);
    exit(0);
}


int main(int argc, char **argv)
{

    fp = fopen("log.txt", "w");
    fprintf(fp, "one\n");

    Signal(SIGINT, sigint_handler);

    int listenfd, connfd;
    struct sockaddr_storage clientaddr;
    socklen_t clientlen;

    listenfd = Open_listenfd(argv[1]);


    while(1) {



        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        fprintf(fp, "two\n");

        char type[MAXLINE], uri[MAXLINE], version[MAXLINE];

        temp = malloc(MAXLINE);
        input = malloc(MAX_OBJECT_SIZE);

        int inputLocation = 0;
        int len = 0;
        fprintf(fp, "three\n");

        while ((len = recv(connfd, temp, MAXLINE, 0)) > 0) {

            int i;
            for(i = 0; i < len; i++) {
                input[inputLocation + i] = temp[i];
            }
            inputLocation += len;
            fprintf(fp, "loop\n");

        }

        sscanf(input, "%s %s %s", type, uri, version);

        fprintf(fp, "four\n");


        fprintf(fp, "%s\n", uri);
        free(input);
        free(temp);

        close(connfd);
    }
    return 0;
}
