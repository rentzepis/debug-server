/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 2016-27542 
 *         Name: CUI HANGQI
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

/* Undefine this if you don't want debugging output */
// #define DEBUG

/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Declarations
 * ■■■■                                                                 ■■■■■■ */
    // #define PORT 3210

    // 
    FILE * file_log;
    sem_t mutex_log, mutex_sem;

    void deal_log_format(char *log_buf, struct sockaddr_in *sockaddr, char *buf);
    void print_log(char *log_format);

    struct thread_args
    {
        int fd;
        struct sockaddr_in addr;
    };
/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */

/*
 * Functions to define
 */
void *process_request(void* vargp);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Implementation in Main function
 * ■■■■                                                                 ■■■■■■ */
    
    Signal(SIGPIPE, SIG_IGN);
    // get proxy log fd
    file_log = Fopen(PROXY_LOG, "a");
    sem_init(&mutex_sem, 0, 1);
    sem_init(&mutex_log, 0, 1);
    

    // decalaration
    int port = atoi(argv[1]);
    pthread_t tid;
    int connfd, clientlen;
    int listenfd = Open_listenfd(port);
    struct sockaddr_in clientaddr;

    printf("Proxy server listening @ port: \033[1;33m%d\033[0m\n", port);
    while(1) {
        clientlen = sizeof(clientaddr);

        // alloc the arg struct & accept
        struct thread_args *argsp = (struct thread_args *) Malloc(sizeof(struct thread_args));
        connfd = Accept(listenfd, (SA *)(&(argsp->addr)), &clientlen); 
        argsp->fd = connfd;

        #ifdef DEBUG
            struct hostent *hp;
            char *client_ip;
            hp = Gethostbyaddr((const char *)&argsp->addr.sin_addr.s_addr, sizeof(argsp->addr.sin_addr.s_addr), AF_INET);
            client_ip = inet_ntoa(argsp->addr.sin_addr);
            printf("\033[1;33m%s conected @ (%s)!\033[0m\n", hp->h_name, client_ip);
            printf("...\n");
        #endif

        // Create a thread
        Pthread_create(&tid, NULL, process_request, (void *)argsp);
    }

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */


    exit(0);
}


/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ *
 * Implementation of custom functions
 * ■■■■                                                                 ■■■■■■ */

// thread handle function
void *process_request(void *vargs) {
    // detached the thread
    Pthread_detach(Pthread_self());
    // pthread_join(pthread_self(), NULL);

    // release memory
    struct thread_args *args = (struct thread_args *)vargs;
    int fd_client = args->fd;
    struct sockaddr_in sock;
    memcpy(&sock, &(args->addr), sizeof(struct sockaddr_in));
    free(args);


    // declaration and init
    char buf[MAXLINE];
    char hostname[MAXLINE], port[MAXLINE], content[MAXLINE];
    rio_t rio_client;
    Rio_readinitb(&rio_client, fd_client);

    rio_t rio_server;
    int fd_server = -1;

    // Main loop for waiting input from client.
    int initFlag = 1;
    size_t n;
    while((n = Rio_readlineb_w(&rio_client, buf, MAXLINE)) != 0) {

        #ifdef DEBUG
            printf("\033[1;34m[%lu]\033[0m got line: %s", pthread_self(), buf);
        #endif

        // parse the input to hostname, pot and content. Error handling and response
        int ss;
        if ((ss = sscanf(buf, "%s %s %s", hostname, port, content)) < 3) {
            if (ss > 0) {
                fprintf(stderr, "No hostname, port, content\n");
                char * errMsg = "\033[0;33m[WARNING]Please input the argument of hostname, port, content!\033[0m\n";
                Rio_writen_w(fd_client, errMsg, strlen(errMsg));
                // Close(fd_client);
                continue;
                // return NULL;
            }else {
                fprintf(stderr, "request error!\n");
                char * errMsg = "\033[1;31m[ERROR]There need a argument or occured a request error\033[0m\n";
                Rio_writen_w(fd_client, errMsg, strlen(errMsg));
                // Close(fd_client);
                continue;
                // return NULL;
            }
        }

        // Create file descriptor and initialize Once!
        if (initFlag) {
            initFlag = 0;
            fd_server = open_clientfd_ts(hostname, atoi(port), &mutex_sem);
            Rio_readinitb(&rio_server, fd_server);
        }

        char line[MAXLINE];
        sprintf(line, "%s\n", content);
        #ifdef DEBUG
            int iport = atoi(port);
            printf("\033[1;30mhost:\033[0m %s, \033[1;30mport:\033[0m %d, \033[1;30mcontent:\033[0m (%d)%s\n", hostname, iport, strlen(line), line);
        #endif

        // write to server & read the response frome server
        Rio_writen_w(fd_server, line, strlen(line));
        size_t nn;
        nn = Rio_readlineb_w(&rio_server, buf, MAXLINE);

        #ifdef DEBUG
            printf("received from server: %s \n", buf);
        #endif
        
        // if the readline not 0
        if (nn != 0) {
            // make log format
            char log_format[MAXLINE];
            deal_log_format(log_format, &sock, buf);

            // print and save log info 
            print_log(log_format);
        }
        
        Rio_writen_w(fd_client, buf, strlen(buf));
    }
   
    // close the file descriptor
    #ifdef DEBUG
        printf("\033[1;31mOne client terminated.\033[0m\n");
    #endif

    // if server didn't initialized
    if (fd_server != -1) {
        Close(fd_client);
        Close(fd_server);
    }

    
    return NULL;
}

// Implement hte format of -> date: clientIP clientPort size echostring & save in log buffer
void deal_log_format(char *log_buf, struct sockaddr_in *sockaddr, char *buf) {
    time_t rawtime;
    struct tm * timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char * tt = asctime(timeinfo);
    tt[strlen(tt)-1] = '\0';

    // get IP address | copy from echoservert.c
    unsigned long haddrp;
    unsigned char a, b, c, d;
    int client_port;
    /* to avoid use inet_ntoa, because it's thread unsafe */
    haddrp = ntohl(sockaddr->sin_addr.s_addr);
    a = haddrp >> 24;
    b = (haddrp >> 16) & 0xff;
    c = (haddrp >> 8) & 0xff;
    d = haddrp & 0xff;
    client_port = ntohs(sockaddr->sin_port);

    // concat all strings
    sprintf(log_buf, "%s: %d.%d.%d.%d %d %3d %s", tt, a,b,c,d, client_port, strlen(buf), buf);
}

// save file and lock the write action.
void print_log(char *log_format) {
    P(&mutex_log);
    fprintf(file_log, "%s", log_format);
    fflush(file_log);
    V(&mutex_log);
    printf("%s", log_format);
}

// For thread safe
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp) {
    int result;

    // printf("Hostname: %s, port: %d", hostname, port);
    P(mutexp);
    result = Open_clientfd(hostname, port);
    V(mutexp);
    
    return result;
}

// New wrappers for rio read and write
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) {
    ssize_t n;
  
    if ((n = rio_readn(fd, ptr, nbytes)) < 0) {
        printf("Warning: rio_readn failed\n");
        return 0;
    }

    return n;
}
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        printf("Warning: rio_readlineb failed\n");
        return 0;
    }

    return rc;
}
void Rio_writen_w(int fd, void *usrbuf, size_t n) {
    if (rio_writen(fd, usrbuf, n) != n) {
	    printf("Warning: rio_writen failed.\n");
    }
}

/* ■■■■                                                                 ■■■■■■ */
/* ■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■■ */
