/*
 * proxy.c - ICS Web proxy
 *
 */

#include "csapp.h"

#define THREADS 16
// ATTENTION: below isn't my intention...
#define MAX_READ_WRITE_ONCE 1

sem_t sem;

/*
 * Function prototypes
 */
int parse_uri(char *uri, char *target_addr, char *path, char *port);
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri,
                      size_t size);

void unix_error_w(char *msg) {
    P(&sem);
    fprintf(stderr, "%s: %s\n", msg, strerror(errno));
    V(&sem);
}

ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t rc;

    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        unix_error_w("Rio_readlineb_w error");
        return 0;
    }
    return rc;
}

ssize_t Rio_readnb_w(rio_t *rp, void *usrbuf, size_t n) {
    ssize_t rc;

    if ((rc = rio_readnb(rp, usrbuf, n)) < 0)
        unix_error_w("Rio_readn_w error");
    return rc;
}

ssize_t Rio_writen_w(int fd, void *usrbuf, size_t n) {
    ssize_t written;

    if ((written = rio_writen(fd, usrbuf, n)) != n) {
        unix_error_w("Rio_writen_w error");
        return 0;
    }
    return written;
}

int Open_clientfd_w(char *hostname, char *port) {
    int rc;

    if ((rc = open_clientfd(hostname, port)) < 0)
        unix_error_w("Open_clientfd error");
    return rc;
}

ssize_t min(ssize_t a, ssize_t b) { return a > b ? b : a; }

ssize_t read_write_request_response(rio_t read_rio, int write_fd) {
    // create the buffer
    char buf[MAXLINE];

    // get the request/response header
    buf[0] = '\0';
    ssize_t content_length = 0, total_length = 0;
    while (strcmp(buf, "\r\n")) {
        ssize_t read_size = Rio_readlineb_w(&read_rio, buf, MAXLINE);
        if (!read_size) {
            return -1;
        }
        if (!Rio_writen_w(write_fd, buf, read_size))
            return -1;

        // accumulate size into a variable
        total_length += read_size;

        // check if the line contains Content-Length header
        if (strncmp(buf, "Content-Length: ", 16) == 0) {
            content_length = atoi(buf + 16);
        }
    }

    // read the request/responce body
    ssize_t current_pos = 0, read_size = 0;
    for (ssize_t i = 0; i < content_length; i += read_size) {
        // make the best use of the buffer
        read_size =
            Rio_readnb_w(&read_rio, buf + current_pos,
                         min(min(MAXLINE - current_pos, content_length - i),
                             MAX_READ_WRITE_ONCE));
        if (read_size < 0) {
            return -1;
        } else if (!read_size) {
            break;
        }

        // increase current_pos, only if the buffer is full will the program
        // write the content into client
        current_pos += read_size;
        // ATTENTION: below isn't my intention...
        if (1 || current_pos == MAXLINE) {
            if (!Rio_writen_w(write_fd, buf, current_pos)) {
                return -1;
            }
            current_pos = 0;
        }

        // accumulate size into a variable
        total_length += read_size;
    }
    // write left bytes into client
    if (current_pos && !Rio_writen_w(write_fd, buf, current_pos)) {
        return -1;
    }

    // return the bytes written
    return total_length;
}

struct read_write_pack {
    rio_t read_rio;
    int write_fd;
    ssize_t *return_value;
};

void *read_write_function_pack(void *args) {
    struct read_write_pack *pack = args;
    *pack->return_value =
        read_write_request_response(pack->read_rio, pack->write_fd);
    return NULL;
}

// function doit, nearly the same function as is in CSAPP
void doit(int listen_fd) {
    // some pre-defined variables
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE],
        log_str[MAXLINE];
    char server_hostname[MAXLINE], server_path[MAXLINE], server_port[MAXLINE];
    rio_t client_rio, server_rio;
    ssize_t total_length = 0;

    int conn_fd = -1, server_fd = -1;
    socklen_t client_length = sizeof(struct sockaddr_storage);
    struct sockaddr_storage client_addr;

    // accept connection from client
    conn_fd = Accept(listen_fd, (SA *)&client_addr, &client_length);

    // read from client
    Rio_readinitb(&client_rio, conn_fd);
    if (!Rio_readlineb_w(&client_rio, buf, MAXLINE))
        goto end;
    if (sscanf(buf, "%s %s %s\r\n", method, uri, version) < 3 ||
        buf[strlen(buf) - 1] != '\n') {
        unix_error_w("Error occurred while parsing header line");
        goto end;
    }

    // parse the uri
    if (parse_uri(uri, server_hostname, server_path, server_port) < 0) {
        unix_error_w("Error occurred while parsing uri");
        goto end;
    }

    // open the server
    server_fd = Open_clientfd_w(server_hostname, server_port);
    if (server_fd < 0) {
        unix_error_w("Error occurred while opening connection to the server");
        goto end;
    }
    // initialize rio of the server
    Rio_readinitb(&server_rio, server_fd);

    // start a new thread to deal with the read-write function
    struct read_write_pack server_pack;
    server_pack.read_rio = server_rio;
    server_pack.write_fd = conn_fd;
    server_pack.return_value = &total_length;

    pthread_t server_thread;
    Pthread_create(&server_thread, NULL, read_write_function_pack,
                   &server_pack);

    // get data from client and send them to server
    sprintf(buf, "%s %s %s\r\n", method, server_path, version);
    if (!Rio_writen_w(server_fd, buf, strlen(buf)))
        goto end;
    if (read_write_request_response(client_rio, server_fd) < 0) {
        unix_error_w("Error occurred while sending request to the server");
        goto end;
    }

    // join the child thread
    Pthread_join(server_thread, NULL);
    if (total_length < 0) {
        unix_error_w("Error occurred while receiving response from the server");
        goto end;
    }

    // log connection info
    format_log_entry(log_str, (struct sockaddr_in *)&client_addr, uri,
                     total_length);
    P(&sem);
    printf("\n%s\n", log_str);
    V(&sem);

end:
    // close server and client
    if (server_fd >= 0)
        Close(server_fd);
    // only close client
    if (conn_fd >= 0)
        Close(conn_fd);
}

void *looping(void *listen_fd) {
    // set detached
    Pthread_detach(Pthread_self());
    // start looping
    while (1) {
        doit(*(int *)listen_fd);
    }
    return NULL;
}

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv) {
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }

    // ignore SIGPIPE
    Signal(SIGPIPE, SIG_IGN);

    // initialize sem
    Sem_init(&sem, 0, 1);

    // open listen file descriptor
    int listen_fd = Open_listenfd(argv[1]);

    // create threads
    pthread_t threads[THREADS];
    for (int i = 0; i < THREADS; i++) {
        Pthread_create(&threads[i], NULL, looping, &listen_fd);
    }

    while (1) {
        // pause the main thread
        Pause();
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
int parse_uri(char *uri, char *hostname, char *pathname, char *port) {
    char *hostbegin;
    char *hostend;
    char *pathbegin;
    int len;

    if (strncasecmp(uri, "http://", 7) != 0) {
        hostname[0] = '\0';
        return -1;
    }

    /* Extract the host name */
    hostbegin = uri + 7;
    hostend = strpbrk(hostbegin, " :/\r\n");
    if (hostend == NULL) {
        len = strlen(hostbegin);
        hostend = hostbegin + len;
    } else {
        len = hostend - hostbegin;
    }
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
        pathname[0] = '/';
        pathname[1] = '\0';
    } else {
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
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri,
                      size_t size) {
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
