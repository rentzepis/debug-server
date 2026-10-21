/*
 * proxy.c - ICS Web proxy
 *
 *
 */

#include "csapp.h"
#include <stdarg.h>
#include <sys/select.h>
#include <assert.h>

/*
    Structs
*/
struct client_info {
    int connfd;
    struct sockaddr_storage clientaddr;
};

struct http_request_line
{
    char method[MAXLINE];
    char uri[MAXLINE];
    char version[MAXLINE];
};

struct http_header
{
    int content_length;
    char raw_header[MAXLINE];
};


/* Rio package wrapper used in proxy */

/**
 * @brief 
 * 
 * @param fd 
 * @param ptr 
 * @param nbytes 
 * @return ssize_t 读的字节数
 */
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);

/**
 * @brief 
 * 
 * @param rp 
 * @param usrbuf 
 * @param n 
 * @return ssize_t 读取的字节数
 */
ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n);

/**
 * @brief 
 * 
 * @param fd 
 * @param usrbuf 
 * @param n 
 * @return ssize_t 写入成功：写入的字节数；写入失败：-1
 */
ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n);

/**
 * @brief 
 * 
 * @param rp 
 * @param usrbuf 
 * @param maxlen 
 * @return ssize_t 读取的字节数
 */
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) ;



/*
 * Core functions 
 */

/**
 * @brief 给定uri，解析出主机名、路径、端口号
 * 
 * @param uri 
 * @param hostname
 * @param path 
 * @param port 
 * @return int 解析成功返回0，否则返回-1
 */
int parse_uri(char *uri, char *hostname, char *path, char *port);

/**
 * @brief 格式化日志输出
 * 
 * @param logstring 
 * @param sockaddr 
 * @param uri 
 * @param size 
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, size_t size);

/**
 * @brief 处理http request
 * 
 * @param fd 连接客户端对应的fd
 */
void handle_http_request(int fd, SA *clientaddr);

/**
 * @brief 读取请求头
 * 
 * @param rp 
 * @param header
 * @ssize_t正常返回读取的字节数，错误返回-1
 */
ssize_t read_http_header(rio_t *rp, struct http_header *header);

/**
 * @brief 返回错误信息
 * 
 * @param fd 
 * @param errnum 
 * @param msg 
 * @param version 
 * @return int 正常返回0，错误返回-1
 */
int serve_error_response(int fd, char *errnum, char *msg, char *version);


/* utils */
/**
 * @brief 在终端输出警告信息，用法类似printf
 * 
 * @param msg 
 * @param ... 
 */
void warning(char *msg, ...);


void *thread(void *vargp) {
    struct client_info* client_infop = (struct client_info *) vargp;
    Pthread_detach(pthread_self());
    handle_http_request(client_infop->connfd, &client_infop->clientaddr);
    Close(client_infop->connfd);
    Free(vargp);
    return NULL;
}
sem_t mutex;
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

    Sem_init(&mutex, 0, 1);
    Signal(SIGPIPE, SIG_IGN);
    /* Open a socket to listen from server */
    int listenfd = Open_listenfd(argv[1]);
    socklen_t clientlen;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    pthread_t tid;
    struct client_info *clientinfop;

    while(1) {
        clientlen = sizeof(struct sockaddr_storage);
        clientinfop = (struct client_info*) Malloc(sizeof(struct client_info));
        clientinfop->connfd = Accept(listenfd, (SA *)& clientinfop->clientaddr, &clientlen);
        printf("Connected to client.\n");
        Pthread_create(&tid, NULL, thread, clientinfop);
    }
    exit(0);
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

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        warning("protocol not http, uri = %s", uri);
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n\0");
    if (hostend == NULL) {
        warning("hostend == NULL");
        return -1;
    }
    len = hostend - hostbegin;
    strncpy(hostname, hostbegin, len);
    hostname[len] = '\0';

    /* Extract the port number */
    if (*hostend == ':') {
        char *p = hostend + 1;
        while (isdigit(*p))
            *port++ = *p++;
        *port = '\0';
    } else {
        strcpy(port, "80");
    }

    /* Extract the path */
    pathbegin = strchr(hostbegin, '/');
    if (pathbegin == NULL) {
        pathname[0] = '\0';
    }
    else {
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
    char host[INET_ADDRSTRLEN];

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    if (inet_ntop(AF_INET, &sockaddr->sin_addr, host, sizeof(host)) == NULL)
        unix_error("Convert sockaddr_in to string representation failed\n");

    /* Return the formatted log entry string */
    sprintf(logstring, "%s: %s %s %zu", time_str, host, uri, size);
}


void handle_http_request(int connfd, SA *clientaddr) {
    char buf[MAXLINE];
    struct http_request_line request_line = {0};
    struct http_header request_header = {0};
    rio_t connfd_rio;
    Rio_readinitb(&connfd_rio, connfd);

    /* Read request line */
    ssize_t request_line_byte_count = Rio_readlineb_w(&connfd_rio, buf, MAXLINE);
    if(request_line_byte_count == 0) {
        return ;
    }
    buf[request_line_byte_count] = '\0';
    if(sscanf(buf, "%s %s %s\r\n", request_line.method, request_line.uri, request_line.version) != 3) {
        warning("parse request line error: ");
        warning("request line = `%s`", buf);
        serve_error_response(connfd, buf, "400", "Bad Request");
        return ;
    }

    /* Parse URI from GET request */
    char hostname[MAXLINE], pathname[MAXLINE], port[MAXLINE];
    if(parse_uri(request_line.uri, hostname, pathname, port) < 0) {
        warning("parse uri error");
        warning("uri = `%s`", request_line.uri);
        serve_error_response(connfd, buf, "400", "Bad Request");

        return ;
    }
    /* Create a socket to connect to end server */
    int clientfd = Open_clientfd(hostname, port);
    ssize_t end_server_response_count = 0;
    rio_t clientfd_rio;
    Rio_readinitb(&clientfd_rio, clientfd);

    /* Forward request line */
    sprintf(buf, "%s %s %s\r\n", request_line.method, pathname, request_line.version);
    if(Rio_writen_w(clientfd, buf, strlen(buf)) < 0) {
        return ;
    }
    
    /* Read http request header and forward */
    if(read_http_header(&connfd_rio, &request_header) < 0) {
        warning("read http header error");
        return ;
    }
    if(Rio_writen_w(clientfd, request_header.raw_header, strlen(request_header.raw_header)) < 0) {
        return ;
    }

    /* Read request body and forward, one byte per time */
    if(request_header.content_length > 0) {
        int content_left_byte = request_header.content_length;
        while(content_left_byte) {
            if(Rio_readnb_w(&connfd_rio, buf, 1) == 0) {
                warning("Read request body error");
                return ;
            }
            if(Rio_writen_w(clientfd, buf, 1) < 0) {
                warning("Forward request body error");
                return ;
            }
            -- content_left_byte;
        }
    }

    /* Read http response line and forward */
    ssize_t response_line_byte_count = Rio_readlineb_w(&clientfd_rio, buf, MAXLINE);
    if(response_line_byte_count == 0) {
        return ;
    }
    end_server_response_count += response_line_byte_count;
    if(Rio_writen_w(connfd, buf, strlen(buf)) < 0) {
        return ;
    }

    /* Read http response header and forward */
    struct http_header response_header = {0};
    ssize_t response_header_byte_count = read_http_header(&clientfd_rio, &response_header);
    if(response_header_byte_count < 0) {
        warning("read http header error");
        return ;
    }
    end_server_response_count += response_header_byte_count;
    if(Rio_writen_w(connfd, response_header.raw_header, strlen(response_header.raw_header)) < 0) {
        return ;
    }
    
    /* Read http response body and forward, one byte per time */
    if(response_header.content_length > 0) {
        int content_left_bytes = response_header.content_length;
        while(content_left_bytes) {
            if(Rio_readnb_w(&clientfd_rio, buf, 1) == 0) {
                warning("read response body error");
                return ;
            }
            ++ end_server_response_count;
            if(Rio_writen_w(connfd, buf, 1) < 0) {
                warning("forward response body error");
                return ;
            }
            -- content_left_bytes;

        }
    }
    
    /* write to log file */
    char log_string[MAXLINE];
    format_log_entry(log_string, clientaddr , request_line.uri, end_server_response_count);
    P(&mutex);
    printf("%s\n", log_string);
    V(&mutex);
}

ssize_t read_http_header(rio_t *rp, struct http_header *header) {
    char linebuf[MAXLINE];
    header->raw_header[0] = '\0';
    int content_length;
    ssize_t byte_count;
    ssize_t total_byte_count = 0;

    while(1) {
        byte_count = Rio_readlineb_w(rp, linebuf, MAXLINE);
        if(byte_count == 0) {
            warning("http header is truncated");
            // 请求头被截断
            return -1;
        }

        linebuf[byte_count] = '\0';
        total_byte_count += byte_count;
        strcat(header->raw_header, linebuf);
        if(!strcmp(linebuf, "\r\n")) {
            break;
        }
        if(sscanf(linebuf, "Content-Length: %d\r\n", &content_length) == 1) {
            header->content_length = content_length;
        }
    }

    return total_byte_count;
}

int serve_error_response(int fd, char *errnum, char *msg, char *version) {
    char body[MAXBUF];
    char buf[MAXBUF];
    sprintf(body, "{\"ok\": false}");

    sprintf(buf, "%s %s %s\r\n", version, errnum, msg);
    if(Rio_writen_w(fd, buf, strlen(buf)) < 0) {
        return -1;
    }
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    if(Rio_writen_w(fd, buf, strlen(buf)) < 0) {
        return -1;
    }
    if(Rio_writen_w(fd, body, strlen(body)) < 0) {
        return -1;
    }

    return 0;
}


/* Rio package wrapper used in below functions */
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes) 
{
    ssize_t n;
  
    if ((n = rio_readn(fd, ptr, nbytes)) < 0) {
        warning("Rio_readn error, nbytes: %d, error: %s", nbytes, strerror(errno));
        return 0;
    }
    
    return n;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) 
{
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0) {
        warning("Rio_readnb error, error: %s", strerror(errno));
        return 0;
    }
    return rc;
}

ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n) 
{
    if (rio_writen(fd, usrbuf, n) != n) {
        warning("Rio_writen error, error: %s", strerror(errno));
        return -1;
    }

    return n;
}


ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) 
{
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        warning("Rio_readlineb error: %s", strerror(errno));
        return 0;
    }
    return rc;
} 

void warning(char *msg, ...) {
    va_list args;
    va_start(args, msg);
    fprintf(stderr, "\033[33m");
    vfprintf(stderr, msg, args);
    fprintf(stderr, "\033[0m\n");
    va_end(args);
}