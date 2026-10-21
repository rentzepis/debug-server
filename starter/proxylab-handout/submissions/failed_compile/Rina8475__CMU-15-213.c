#include <stdio.h>
#include <assert.h>
#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

/* the max number of fields in a header */
#define MAXFIELDLEN 32

/* the max size of a token */
#define TOKENSIZE   64

/* the max size of a field */
#define FIELDSIZE   2048

/* the number of threads in thread pool */
#define NTHREADS    4

/* the size of buf in struct sbuf */
#define SBUFSIZE    16

/* this struct store info about URI */
struct URI {
    char host[FIELDSIZE];
    char port[TOKENSIZE];
    char path[FIELDSIZE];
};

/* this struct store info about the start line of header */
struct Starter {
    char method[TOKENSIZE];
    struct URI uri;
    char version[TOKENSIZE];
};

/* this struct store info about the name - value pair after starter */
struct Field {
    char name[TOKENSIZE];
    char value[FIELDSIZE];
};

/* this struct store info about the header */
struct Header {
    struct Starter starter;
    struct Field *field;
    unsigned fieldlen;
};

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";
static const char *http_version = "HTTP/1.0";

/* Shared buffer of connected descriptors */
sbuf_t sbuf;    

/* Shared cache */
cache_t cache;

void doit(int fd);
int read_header(int fd, char *buf, unsigned buflen);
int parse_header(struct Header *header, char *msg);
int parse_starter(char *start, struct Starter *startp);
void modify_header(struct Header *header);
void Set_field(struct Header *header, char *name, char *value);
char *set_field(struct Header *header, char *name, char *value);
int generate_header(char *buf, struct Header *header);
int try_cache(int clientfd, char *buf, int buflen);
void write_back(int connfd, int clientfd, char *buf, int buflen);
int split_line(char *line, char *spliter, char *tokens[], unsigned tokenlen);

void sigpipe_handler(int sig);
int Rio_readlineb_s(rio_t *rp, void *usrbuf, size_t maxlen);
int Rio_writen_s(int fd, void *usrbuf, size_t n);
void *thread(void *fd);

int main(int argc, char *argv[])
{
    unsigned listenfd, connfd;
    struct sockaddr_storage clientaddr;
    socklen_t clientlen;
    char host[FIELDSIZE], port[TOKENSIZE];
    pthread_t tid;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        return 1;
    }

    /* install signal handler */
    Signal(SIGPIPE, sigpipe_handler);
    /* init cache */
    cache_init(&cache);
    /* init sbuf */
    sbuf_init(&sbuf, SBUFSIZE);
    /* create threads */
    for (int i = 0; i < NTHREADS; i += 1) {
        Pthread_create(&tid, NULL, thread, NULL);
    }
    
    /* init proxy */
    listenfd = Open_listenfd(argv[1]);
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, host, FIELDSIZE, port, TOKENSIZE, 0);
        printf("Connected to (%s %s)\n", host, port);
        sbuf_insert(&sbuf, connfd);
    }
    return 0;
}

/* doit - read the http header from user, modify it, and send it to the remote
 * server, receive the msg from remote server and send it to the user */
void doit(int fd) {
    struct Header header;
    char message[MAXLINE], *host, *port, uri[FIELDSIZE];
    int msglen, clientfd, buflen, urilen;
    char cachebuf[MAX_OBJECT_SIZE];

    /* read the http msg from user */
    if ((msglen = read_header(fd, message, MAXLINE)) < 0) {
        return;
    } 
    printf("Header is:\n%s", message);
    /* parse the header */
    if (parse_header(&header, message) < 0) {
        return;
    }
    host = header.starter.uri.host;
    port = header.starter.uri.port;
    /* generate URI */
    urilen = sprintf(uri, "%s:%s%s", host, port, header.starter.uri.path);
    if ((buflen = cache_read(&cache, uri, cachebuf)) >= 0) {
        /* if this msg has been cached */
        printf("This uri has been cached.\n");
        Rio_writen_s(fd, cachebuf, buflen);
        free(header.field);         /* need free header */
        return;
    }
    /* modify the header */
    modify_header(&header);
    /* generate the header to send */
    msglen = generate_header(message, &header);
    printf("Generate header:\n%s", message);
    /* connect to remote server */
    clientfd = Open_clientfd(host, port);
    printf("Connected to (%s %s)\n", host, port);
    /* send correct header to remote server  */
    Rio_writen(clientfd, message, msglen);
    /* try to cache msg received from remote server */
    if ((buflen = try_cache(clientfd, cachebuf, MAX_OBJECT_SIZE)) > 0) {
        /* cache success */
        printf("This uri has been cached successfully.\n");
        cache_write(&cache, uri, urilen, cachebuf, buflen);
        Rio_writen_s(fd, cachebuf, buflen);
    } else {
        /* cache failed */
        write_back(fd, clientfd, cachebuf, -buflen);
    }
    /* close fd and free the fields */
    free(header.field);
    Close(clientfd);
}

/* read_header - read a header from user. return the length of bytes read in BUF
 * if the length over BUF, then return -1 */
int read_header(int fd, char *buf, unsigned buflen) {
    rio_t rio;
    int length, readcnt;
    char line[FIELDSIZE];

    Rio_readinitb(&rio, fd);
    buf[0] = '\0';
    length = 0;
    while ((readcnt = Rio_readlineb_s(&rio, line, FIELDSIZE)) > 0) {
        length += readcnt;
        sprintf(buf, "%s%s", buf, line);
        if (strcmp(line, "\r\n") == 0) {
            break;
        }
    }
    assert(length < buflen-1);
    return (readcnt > 0) ? length : -1;
}

/* parse_header - parse http header to struct Header, if error, then return -1 */
int parse_header(struct Header *header, char *msg) {
    char *ptr, *lines[MAXFIELDLEN];
    unsigned lines_len;
    
    /* parse the msg into lines */
    ptr = strstr(msg, "\r\n\r\n");
    assert(ptr != NULL);
    *ptr = '\0';                /* remove the last '\r\n\r\n' */
    lines_len = split_line(msg, "\r\n", lines, MAXFIELDLEN);
    /* parse the first line */
    if (parse_starter(lines[0], &(header->starter)) < 0) {
        return -1;
    }
    /* parse the rest lines */
    header->field = (struct Field *) Malloc((lines_len-1) * sizeof(struct Field));
    header->fieldlen = lines_len-1;
    for (int i = 1; i < lines_len; i += 1) {
        if ((ptr = strchr(lines[i], ':')) == NULL) {
            return -1;          /* a malformed request */
        }
        *ptr = '\0';
        strcpy(header->field[i-1].name, lines[i]);
        strcpy(header->field[i-1].value, ptr+2);
    }
    return 0;
}

/* parse_starter - parse the first line to struct Starter */
int parse_starter(char *start, struct Starter *startp) {
    char *ptr, *uri, *tokens[MAXFIELDLEN];

    if (split_line(start, " ", tokens, MAXFIELDLEN) != 3) {
        return -1;          /* a malformed request */
    }
    strcpy(startp->method, tokens[0]);
    strcpy(startp->version, http_version);
    /* parse the uri */
    uri = strstr(tokens[1], "//");
    uri += 2;       /* remove the str "http://" */
    ptr = strchr(uri, '/');
    strncpy(startp->uri.path, ptr, FIELDSIZE);          /* copy path */
    *ptr = '\0';
    if ((ptr = strchr(uri, ':')) != NULL) {             /* copy port */
        *ptr = '\0';
        strncpy(startp->uri.port, ptr+1, TOKENSIZE);
    } else {
        strcpy(startp->uri.port, "80");         /* default port is 80 */
    }
    strncpy(startp->uri.host, uri, FIELDSIZE);          /* copy host */
    return 0;
}

/* modify_header - modify the value of header */
void modify_header(struct Header *header) {
    char buffer[FIELDSIZE];
    unsigned flag;
    
    flag = (strcmp(header->starter.uri.port, "80") == 0);
    snprintf(buffer, FIELDSIZE, "%s%s%s", header->starter.uri.host, 
        flag ? "" : ":", flag ? "" : header->starter.uri.port);
    Set_field(header, "Host", buffer);
    Set_field(header, "User-Agent", (char *) user_agent_hdr);
    Set_field(header, "Connection", "close");
    Set_field(header, "Proxy-Connection", "close");
}

/* Set_field - wrapped functio, if not find such NAME, then creat a new field */
void Set_field(struct Header *header, char *name, char *value) {
    if (set_field(header, name, value) == NULL) {
        header->fieldlen += 1;
        header->field = (struct Field *) Realloc(header->field, 
            header->fieldlen * sizeof(struct Field));
        strcpy(header->field[header->fieldlen-1].name, name);
        strcpy(header->field[header->fieldlen-1].value, value);
    }
}

/* set_field - set the VALUE to the field NAME, return the value after set
 * if not find such NAME in header, then return NULL */
char *set_field(struct Header *header, char *name, char *value) {
    for (unsigned i = 0; i < header->fieldlen; i += 1) {
        if (strcmp(header->field[i].name, name) == 0) {
            strcpy(header->field[i].value, value);
            return header->field[i].value;
        }
    }
    return NULL;
}

/* generate_header - generate a http header by struct HEADER, write this msg into BUF
 * return the number of bytes in BUF */
int generate_header(char *buf, struct Header *header) {
    sprintf(buf, "%s %s %s\r\n", header->starter.method, header->starter.uri.path, 
        header->starter.version);
    for (int i = 0; i < header->fieldlen; i += 1) {
        sprintf(buf, "%s%s: %s\r\n", buf, header->field[i].name, header->field[i].value);
    }
    return sprintf(buf, "%s\r\n", buf);
}

/* try_cache - receive msg from CLIENTFD, and try to cache it into BUF,
 * if BUF has enough space for store this msg, then return the length of
 * this msg, else return a negtive number, which represent the length of
 * msg has been read into BUF */
int try_cache(int clientfd, char *buf, int buflen) {
    rio_t rio;
    char line[FIELDSIZE];
    int readcnt, idx;

    idx = 0;
    Rio_readinitb(&rio, clientfd);
    while ((readcnt = Rio_readlineb_s(&rio, line, FIELDSIZE)) > 0) {
        if (idx + readcnt > buflen) {
            return -idx;
        }
        memcpy(buf+idx, line, readcnt);
        idx += readcnt;
    }
    /* assume that the remote server will not close this connection */
    return idx;
}

/* write_back - if the BUF do not have enough space to cache, then call
 * this function. first write the msg in BUF to CONNFD, then read msg from
 * CLIENTFD, and write it into CONNFD */
void write_back(int connfd, int clientfd, char *buf, int buflen) {
    rio_t rio;
    char line[FIELDSIZE];
    int readcnt;

    /* write msg in BUF into CONNFD */
    if (Rio_writen_s(connfd, buf, buflen) < 0) {
        return;         /* if the user closed connection */
    }
    /* read msg from CLIENTFD and write it into CONNFD */
    Rio_readinitb(&rio, clientfd);
    while ((readcnt = Rio_readlineb_s(&rio, line, FIELDSIZE)) > 0) {
        if (Rio_writen_s(connfd, line, readcnt) < 0) {
            return;      /* received signal SIGPIPE */
        }
    }
}

/* split_line - split the LINE by SPLITER, and store the tokens into TOKENS 
 * return the number of tokens 
 * P.S. this function will change the value of LINE */
int split_line(char *line, char *spliter, char *tokens[], unsigned tokenlen) {
    unsigned spliterlen, idx;
    char *ptr;

    spliterlen = strlen(spliter);
    for (idx = 0; idx < tokenlen-1; idx += 1) {
        /* remove the possible SPLITER in the begin of LINE */
        while (strncmp(line, spliter, spliterlen) == 0) {
            line += spliterlen;
        }
        if ((ptr = strstr(line, spliter)) == NULL) {
            break;
        }
        *ptr = '\0';
        tokens[idx] = line;
        line = ptr + spliterlen;
    }
    /* store the rest of LINE */
    if (idx < tokenlen-1 && *line != '\0') {
        tokens[idx] = line;
        idx += 1;
    }
    tokens[idx] = NULL;
    return idx;
}

/* sigpipe_handler - the handler of sigpipe */
void sigpipe_handler(int sig) {
    fprintf(stdout, "Received signal SIGPIPE.\n");
}

/* Rio_writen_s - wrapped function, if writen operation return -1, then check
 * errno, if errno equal to EPIPE, then just return -1, else terminate this 
 * process. if every thing correct, then return 0 */
int Rio_writen_s(int fd, void *usrbuf, size_t n) {
    if (rio_writen(fd, usrbuf, n) < 0) {
        if (errno == EPIPE) {
            return -1;
        } else {
            unix_error("Rio_writen error");
        }
    }
    return 0;
}

/* Rio_readlineb_s - wrapped function, if readlineb operation return -1, 
 * then check errno, if errno equal to ECONNRESET, then just return -1, 
 * else terminate this process. if every thing correct, then return the 
 * number of bytes read. */
int Rio_readlineb_s(rio_t *rp, void *usrbuf, size_t maxlen) {
    ssize_t rc;
    
    if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0) {
        if (errno == ECONNRESET) {
            return -1;
        } else {
            unix_error("Rio_readlineb error");
        }
    }
    return (int) rc;
}

/* thread - a routine that every thread will call */
void *thread(void *arg) {
    int connfd; 
    pthread_t tid;

    tid = pthread_self();
    Pthread_detach(tid);
    while (1) {         /* endless loop */
        connfd = sbuf_remove(&sbuf);
        printf("Thread [%lu] is dealing fd %d.\n", tid, connfd);
        doit(connfd);
        Close(connfd);
    }
}
