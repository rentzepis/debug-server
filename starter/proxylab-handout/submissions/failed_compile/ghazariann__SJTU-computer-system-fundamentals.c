#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>

/*
 * Function prototypes
 */
int RESPONSE_BODY_MAX_SIZE = 102400; // 102400 is the max size of response body

int parse_and_send_header(int fd, rio_t *rio, int *cnt);
void *thread(void *args);
void doit(int fd, struct sockaddr_in *sockaddr);
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);


// wrapper functions defined in csapp.c
int Open_clientfd_w(char *hostname, char *port);
int Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
int Rio_writen_w(int fd, void *usrbuf, size_t n);
int Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);

typedef struct
{
    struct sockaddr_in address;
    int fd;

} thread_args;

sem_t o, p, s;

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    /* Check arguments */
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    // ignore SIGPIPE
    Signal(SIGPIPE, SIG_IGN);

    // init semaphores
    Sem_init(&o, 0, 1);
    Sem_init(&p, 0, 1);
    Sem_init(&s, 0, 1);

    // act as server and listen client
    listenfd = Open_listenfd(argv[1]);
    clientlen = sizeof(clientaddr);

    while (1)
    {   
        // accept client
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 
        thread_args *args = Malloc(sizeof(thread_args));
        args->fd = connfd;
        // copy client address
        memcpy(&args->address, &clientaddr, sizeof(struct sockaddr_in)); 
        pthread_t tid;
        // create thread
        pthread_create(&tid, NULL, thread, args);
    }

    exit(0);
}

void *thread(void *vargp)
{
    pthread_detach(pthread_self());
    int fd = ((thread_args *)vargp)->fd;
    struct sockaddr_in *addr = &((thread_args *)vargp)->address;
    doit(fd, addr);
    free(vargp);
    return NULL;
}

int parse_and_send_header(int fd, rio_t *rp, int *cnt)
{
    char header_buffer[MAXLINE];
    int content_length, bytes_read, total_bytes;

    content_length = 0;
    total_bytes = 0;
    if ((bytes_read = Rio_readlineb_w(rp, header_buffer, MAXLINE)) <= 0)
        return -1;
    while (strcmp(header_buffer, "\r\n"))
    {
        total_bytes += bytes_read;
        if (strcasestr(header_buffer, "Content-Length: "))
        {
            sscanf(header_buffer + strlen("Content-Length: "), "%d", &content_length);
        }
      
        if (Rio_writen_w(fd, header_buffer, strlen(header_buffer)) <= 0)
            return -1;
        if ((bytes_read = Rio_readlineb_w(rp, header_buffer, MAXLINE)) <= 0)
            return -1;
        if (header_buffer[strlen(header_buffer) - 1] != '\n')
            return -1;
    }
    total_bytes += bytes_read;
    if (Rio_writen_w(fd, header_buffer, strlen(header_buffer)) <= 0)
        return -1;
    if (cnt != NULL)
        *cnt += total_bytes;
    
    return content_length;
}

void doit(int fd, struct sockaddr_in *sockaddr)
{
    int server_fd;
    int bytes_read, total_bytes, content_length;
    char buffer[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
    char request_line[MAXLINE];
    char response_body[RESPONSE_BODY_MAX_SIZE];
    char log_entry[MAXLINE];
    rio_t rio_obj;

    rio_readinitb(&rio_obj, fd);
    /* Parse the request header */
    if (Rio_readlineb_w(&rio_obj, buffer, MAXLINE) <= 0)
    {
        fprintf(stderr, "Error: Unable to read request line\n");
        close(fd);
        return;
    }

    if (sscanf(buffer, "%s %s %s", method, uri, version) != 3)
    {
        fprintf(stderr, "Error: sscanf failed to parse request line\n");
        close(fd);
        return;
    }

    if (parse_uri(uri, hostname, pathname, port) < 0)
    {
        fprintf(stderr, "Error: Invalid URL\n");
        close(fd);
        return;
    }

    /* Open server file descriptor */
    P(&o);
    if ((server_fd = Open_clientfd_w(hostname, port)) < 0) 
    {
        V(&o);
        fprintf(stderr, "Error: Failed to connect to server\n");
        close(fd);
        return;
    }
    V(&o);
    if (pathname[0] == '\0')
    {   
        pathname[0] = '/';
        pathname[1] = '\0';
    }
    P(&s);
    sprintf(request_line, "%s %s %s\r\n", method, pathname, version);
    V(&s);

    /* Send request */
    if (Rio_writen_w(server_fd, request_line, strlen(request_line)) <= 0)
    {
        fprintf(stderr, "Error: Failed to send request to server\n");
        close(fd);
        close(server_fd);
        return;
    }

    /* Read and send header */
    content_length = parse_and_send_header(server_fd, &rio_obj, NULL);
    if (content_length < 0)
    {
        fprintf(stderr, "Error: Failed to parse header or send to server\n");
        close(fd);
        close(server_fd);
        return;
    }

    /* Send body if the request method is POST */
    if (strcasecmp(method, "POST") == 0)
    {
        while (content_length > 0)
        {
            if (Rio_readnb_w(&rio_obj, response_body, content_length > RESPONSE_BODY_MAX_SIZE ? RESPONSE_BODY_MAX_SIZE : content_length) <= 0)
            {
                fprintf(stderr, "Error: Failed to read response body\n");
                close(fd);
                close(server_fd);
                return;
            }
            if (Rio_writen_w(server_fd, response_body, content_length > RESPONSE_BODY_MAX_SIZE ? RESPONSE_BODY_MAX_SIZE : content_length) <= 0)
            {
                fprintf(stderr, "Error: Failed to send response body to server\n");
                close(fd);
                close(server_fd);
                return;
            }
            content_length -= RESPONSE_BODY_MAX_SIZE; 
        }
    }

    rio_readinitb(&rio_obj, server_fd);

    /* Get the response and send it to the client */
    total_bytes = 0;
    content_length = parse_and_send_header(fd, &rio_obj, &total_bytes);
    if (content_length < 0)
    {
        fprintf(stderr, "Error: Failed to parse response header or send to client\n");
        close(fd);
        close(server_fd);
        return;
    }

    /* Get the body */
    total_bytes += content_length;
    while (content_length > 0)
    {
        if (Rio_readnb_w(&rio_obj, response_body, 1) <= 0)
        {
            fprintf(stderr, "Error: Failed to read response body\n");
            close(fd);
            close(server_fd);
            return;
        }
        if (Rio_writen_w(fd, response_body, 1) <= 0)
        {
            fprintf(stderr, "Error: Failed to send response body to client\n");
            close(fd);
            close(server_fd);
            return;
        }
        content_length--;
    }

    close(fd);
    close(server_fd);

    /* Log the request if it was successful */
    if (total_bytes > 0)
    {
        format_log_entry(log_entry, sockaddr, uri, total_bytes);
    }
    P(&p);
    printf("%s\n", log_entry);
    fflush(stdout);
    V(&p);
}

/*
 * parse_uri - URI parser
 *
 * Given a URI from an HTTP proxy GET request (i.e., a URL), extract
 * the host name, path name, and port.  The memory for hostname and
 * pathname must already be allocated and should be at least MAXLINE
 * bytes. Return -1 if there are any problems.
 */
int parse_uri(char *uri, char *hostname, char *pathname, char *port)
{
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0)
    {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL)
        return -1;
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':')
    {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    }
    else
    {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL)
    {
        pathname[0] = '\0';
    }
    else
    {
        strcpy(pathname, pathbegin);
    }

    return 0;
}

/*
 * format_log_entry - Create a formatted log entry in logstring.
 *
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), the number of bytes
 * from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr,
                      char *uri, size_t size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /*
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 12, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;

    /* Return the formatted log entry string */
    P(&s);
    sprintf(logstring, "%s: %d.%d.%d.%d %s %zu", time_str, a, b, c, d, uri, size);
    V(&s);
}