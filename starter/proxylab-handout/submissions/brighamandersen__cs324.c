#include "csapp.h"
#include <stdio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include<errno.h>
#include <netdb.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

// curl -v --proxy http://localhost:23080 http://localhost:23081/home.html

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define MAX_STATES 100
#define MAX_EVENTS 100
#define MAX_HOST_SIZE 1024
#define MAX_PORT_SIZE 16
#define MAX_METHOD_SIZE 32
#define MAX_URI_SIZE 4096


// State enums
#define STATE_READ_REQ 1
#define STATE_SEND_REQ 2
#define STATE_READ_RES 3
#define STATE_SEND_RES 4

// Socket enums
#define READING 1
#define WRITING 2


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr = "Connection: close\r\n";
static const char *proxy_conn_hdr = "Proxy-Connection: close\r\n";

typedef struct {
    int client_socket_fd;
    int server_socket_fd;
    char host[MAX_HOST_SIZE];
    char port[MAX_PORT_SIZE];
    char client_request[MAX_OBJECT_SIZE];
    char server_request[MAX_OBJECT_SIZE];
    char server_response[MAX_OBJECT_SIZE];
    int state;
    int bytes_read_from_client;
    int bytes_to_write_server;
    int bytes_written_to_server;
    int bytes_read_from_server; 
    int bytes_to_write_client;
    int bytes_written_to_client;
} conn_state_t;

conn_state_t conn_states[MAX_STATES];

void make_socket_nonblocking(int fd) {
    if (fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) < 0) {
        unix_error("error setting socket option\n");
    }
}

void connect_to_client(int listenfd, conn_state_t *conn_state, int efd) {
    struct sockaddr_in in_addr;
    unsigned int addr_size = sizeof(in_addr);
    char hbuf[MAXLINE], sbuf[MAXLINE];


    int connfd = 0;
    while((connfd = accept(listenfd, (struct sockaddr *)(&in_addr), &addr_size)) > 0) {

        // if (errno == EAGAIN || errno == EWOULDBLOCK) {  // Just means you need to stop and come back later
        //     return; 
        // }

        /* get the client's IP addr and port num */
        int s = getnameinfo ((struct sockaddr *)&in_addr, addr_size,
                                    hbuf, sizeof hbuf,
                                    sbuf, sizeof sbuf,
                                    NI_NUMERICHOST | NI_NUMERICSERV);
        if (s == 0) {
            printf("Accepted client connection on descriptor %d (host=%s, port=%s)\n", connfd, hbuf, sbuf);
        }

        // Set conn_state's client socket fd to be the one returned from accept
        conn_state->client_socket_fd = connfd;

        // Register struct as non-blocking
        make_socket_nonblocking(conn_state->client_socket_fd);

        // Register client socket for reading for first time (IN, ADD)
        struct epoll_event tmp_event;
        tmp_event.data.ptr = conn_state;
        tmp_event.events = EPOLLIN | EPOLLET;
        if (epoll_ctl(efd, EPOLL_CTL_ADD, conn_state->client_socket_fd, &tmp_event) < 0) {
            fprintf(stderr, "Couldn't register client socket for reading with epoll\n");
            exit(EXIT_FAILURE);
        }
    }
}

void connect_to_server(conn_state_t *conn_state) {
    struct addrinfo hints;
    struct addrinfo *result, *rp;
    int sfd;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;	// IPv4
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = 0;
    hints.ai_protocol = 0;	// TCP

    // Connect to server

    // Returns a list of address structures, so we try each address until we successfully connect
    Getaddrinfo(conn_state->host, conn_state->port, &hints, &result);

    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                rp->ai_protocol);
        if (sfd == -1)
            continue;
        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) {
            printf("Accepted server connection on descriptor %d (host=%s, port=%s)\n", sfd, conn_state->host, conn_state->port);
            break;                  /* Success */
        }
        close(sfd);
    }
    if (rp == NULL) {               /* No address succeeded */
        fprintf(stderr, "Could not connect\n");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(result);           /* No longer needed */

    // Set conn_state's server socket fd to be the one returned from connect
    conn_state->server_socket_fd = sfd;
}

// Initialize event data for new event to be sent through proxy
void init_conn_state(conn_state_t *conn_state) {
    conn_state->client_socket_fd = 0;
    conn_state->server_socket_fd = 0;
    memset(conn_state->host, 0, MAX_OBJECT_SIZE);
    memset(conn_state->port, 0, MAX_OBJECT_SIZE);
    memset(conn_state->client_request, 0, MAX_OBJECT_SIZE);
    memset(conn_state->server_request, 0, MAX_OBJECT_SIZE);
    memset(conn_state->server_response, 0, MAX_OBJECT_SIZE);
    conn_state->state = STATE_READ_REQ;
    conn_state->bytes_read_from_client = 0;
    conn_state->bytes_to_write_server = 0;
    conn_state->bytes_written_to_server = 0;
    conn_state->bytes_read_from_server = 0;
    conn_state->bytes_to_write_client = 0;
    conn_state->bytes_written_to_client = 0;
}

int is_complete_request(const char *request) {
    char *found_ptr = strstr(request, "\r\n\r\n");
    if (found_ptr != NULL) {
        return 1;
    }
    return 0;
}

void reformat_client_request(conn_state_t *conn_state) {
    // If client has not sent the full request, return 0 to show the request is not complete.
    if (is_complete_request(conn_state->client_request) == 0) {
        return;
    }

    char* method = (char*) malloc(MAX_METHOD_SIZE);
    char* uri = (char*) malloc(MAX_URI_SIZE);

    /*
     *		Parse request to get the method, hostname, port, uri
     */
    
    // Make non-const request variable
    char temp_request[MAX_OBJECT_SIZE];
    strcpy(temp_request, conn_state->client_request);

    // Grab method
    char* temp_ptr = strtok(temp_request, " ");
    strcpy(method, temp_ptr);

    // Grab entire path
    temp_ptr = temp_request + strlen(temp_ptr) + 1;  // Move past method
    temp_ptr += 7;	// Move past http://
    temp_ptr = strtok(temp_ptr, " ");

    // Grab everything before slash (Hostname and Port)
    char host_port_str[MAX_HOST_SIZE + MAX_PORT_SIZE];
    strcpy(host_port_str, temp_ptr);
    strtok(host_port_str, "/");

    // Host and Port
    char* port_str = strstr(host_port_str, ":");
    char host_str[MAX_HOST_SIZE];
    strcpy(host_str, host_port_str);

    // If port was specified
    if (port_str != NULL) {
        strtok(host_str, ":");
        strcpy(conn_state->host, host_str);
        strcpy(conn_state->port, port_str + 1);	// Copy port_str into port, skipping the : colon
    } 
    // If just hostname
    else {	// If not colon, make port the default
        strcpy(conn_state->host, host_str);
        strcpy(conn_state->port, "80");	// Default port number
    }

    // Grab everything after slash (URI), if there is a specific uri
    if (strstr(temp_ptr, "/")) {
        char* uri_str = temp_ptr + strlen(host_port_str) + 1;  // Make uri be everything past the host, port and slash (the +1)
        strcpy(uri, uri_str);
    }

    /*
     *		Make new request
     */

    // Concat first line
    strcat(conn_state->server_request, method);
    strcat(conn_state->server_request, " /"); // Space and slash for uri
    strcat(conn_state->server_request, uri);
    strcat(conn_state->server_request, " HTTP/1.0");	// Version
    strcat(conn_state->server_request, "\r\n");	// End line

    // Concat second line
    strcat(conn_state->server_request, "Host: ");
    strcat(conn_state->server_request, conn_state->host);
    strcat(conn_state->server_request, ":");
    strcat(conn_state->server_request, conn_state->port);
    strcat(conn_state->server_request, "\r\n");	// End line

    // Concat hard-coded headers
    strcat(conn_state->server_request, user_agent_hdr);
    strcat(conn_state->server_request, conn_hdr);
    strcat(conn_state->server_request, proxy_conn_hdr);

    // Add final \r\n to signify end of file
    strcat(conn_state->server_request, "\r\n");

    free(method);
    free(uri);

    conn_state->bytes_to_write_server = strlen(conn_state->server_request);
}

// 1.  Client -> Proxy
void read_request(conn_state_t *conn_state, int efd) {
    printf("read_request()\n");

    // Loop while it's not \r\n\r\n
    // Call read, and pass in the fd you returned from calls above
    int cur_read = 0;
    while(1) {
        cur_read = read(conn_state->client_socket_fd, conn_state->client_request + conn_state->bytes_read_from_client, MAX_OBJECT_SIZE - conn_state->bytes_read_from_client);
        if (cur_read > 0)
            conn_state->bytes_read_from_client += cur_read;

        if (cur_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {  // Just means you need to stop and come back later
                return; 
            } 
            // Error -- so cancel client request, deregister socket, and break out
            // Close file descriptors, close epoll instance
            close(conn_state->client_socket_fd);
            close(conn_state->server_socket_fd);    // FIXME - May not need because hasn't been opened
            return; 
        }

        if (is_complete_request(conn_state->client_request)) {    // Done reading
            break;
        }
    }

    printf("client_req: %s\n", conn_state->client_request);

    // Convert to server_request
    reformat_client_request(conn_state);

    printf("server_req: %s\n", conn_state->server_request);

    // Set up a new socket for server
    connect_to_server(conn_state);

    // Configure server socket as non-blocking
    make_socket_nonblocking(conn_state->server_socket_fd);

    // Register the server socket w/ epoll instance for writing (OUT, ADD)
    struct epoll_event tmp_event;
    tmp_event.data.ptr = conn_state;
    tmp_event.events = EPOLLOUT | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, conn_state->server_socket_fd, &tmp_event) < 0) {
        fprintf(stderr, "Couldn't register server socket for writing with epoll\n");
        exit(EXIT_FAILURE);
    }

    // Set state to next state
    conn_state->state = STATE_SEND_REQ;
}

// 2.  Proxy -> Server
void send_request(conn_state_t *conn_state, int efd) {
    printf("send_request()\n");

    // Call write to write the bytes received from client to the server
    int chars_written = 0;
    while ((chars_written = write(conn_state->server_socket_fd, conn_state->server_request + conn_state->bytes_written_to_server, conn_state->bytes_to_write_server - conn_state->bytes_written_to_server)) > 0) {
        conn_state->bytes_written_to_server += chars_written;
    }

    if (chars_written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {  // Just means you need to stop and come back later
            return; 
        } 
        // Error -- so cancel client request, deregister socket, and break out
        // Close file descriptors, close epoll instance
        close(conn_state->client_socket_fd);
        close(conn_state->server_socket_fd);
        return; 
    }

    // Register the server socket with the epoll instance for reading (IN, MOD)
    struct epoll_event tmp_event;
    tmp_event.data.ptr = conn_state;
    tmp_event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_MOD, conn_state->server_socket_fd, &tmp_event) < 0) {
        fprintf(stderr, "Couldn't register server socket for reading with epoll\n");
        exit(EXIT_FAILURE);
    }

    // Set state to next state
    conn_state->state = STATE_READ_RES;
}

// 3.  Server -> Proxy
void read_response(conn_state_t *conn_state, int efd) {
    printf("read_response()\n");

    // Loop while the return val from read is not 0
    int cur_read = 0;
    while ((cur_read = read(conn_state->server_socket_fd, conn_state->server_response + conn_state->bytes_read_from_server, MAX_OBJECT_SIZE - conn_state->bytes_read_from_server)) > 0) {
        conn_state->bytes_read_from_server += cur_read;
    }

    // Copy bytes read from server to bytes to write client
    conn_state->bytes_to_write_client = conn_state->bytes_read_from_server;

    if (cur_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {  // Just means you need to stop and come back later
            return; 
        } 
        // Error -- so cancel client request, deregister socket, and break out
        // Close file descriptors, close epoll instance
        close(conn_state->client_socket_fd);
        close(conn_state->server_socket_fd);
        return; 
    }

    // Close the server socket now that we don't need it
    close(conn_state->server_socket_fd);
    
    printf("Server response: %s\n", conn_state->server_response);
    printf("Bytes read from server: %i\n", conn_state->bytes_read_from_server);

    // Register the client socket with the epoll instance for writing (OUT, MOD)
    struct epoll_event tmp_event;
    tmp_event.data.ptr = conn_state;
    tmp_event.events = EPOLLOUT | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_MOD, conn_state->client_socket_fd, &tmp_event) < 0) {
        fprintf(stderr, "Couldn't register client socket for writing with epoll\n");
        exit(EXIT_FAILURE);
    }

    // Set state to next state
    conn_state->state = STATE_SEND_RES;
}

// 4.  Proxy -> Client
void send_response(conn_state_t *conn_state, int efd) {
    printf("send_response()\n");

    // Call write to write bytes received from server to the client
    int chars_written = 0;
    while ((chars_written = write(conn_state->client_socket_fd, conn_state->server_response + conn_state->bytes_written_to_client, conn_state->bytes_to_write_client - conn_state->bytes_written_to_client)) > 0) {
        conn_state->bytes_written_to_client += chars_written;
    }

    if (chars_written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {  // Just means you need to stop and come back later
            return; 
        } 
        // Error -- so cancel client request, deregister socket, and break out
        // Close file descriptors, close epoll instance
        close(conn_state->client_socket_fd);
        return; 
    }

    printf("Response from server to client was: %s", conn_state->server_response);

    // Close file descriptors, close epoll instance
    close(conn_state->client_socket_fd);
}

int main(int argc, char **argv) {
    int efd, listenfd;
    struct epoll_event event, *events;

    // Return if bad arguments
    if (argc < 2) {
        printf("Usage: %s portnumber\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Create an epoll instance
    if((efd = epoll_create1(0)) < 0) {
        printf("Unable to create epoll fd\n");
        exit(EXIT_FAILURE);
    }

    // Set up listen socket
    listenfd = Open_listenfd(argv[1]);

    // Make listen socket non-blocking
    make_socket_nonblocking(listenfd);

    // Register listen socket with epoll instance for reading (IN, ADD)
    conn_state_t listen_conn_state;
    init_conn_state(&listen_conn_state);
    listen_conn_state.client_socket_fd = listenfd;
    event.data.ptr = &listen_conn_state;
    event.events = EPOLLIN | EPOLLET;
    if (epoll_ctl(efd, EPOLL_CTL_ADD, listenfd, &event) < 0) {
        fprintf(stderr, "Couldn't register listen socket for reading with epoll\n");
        exit(EXIT_FAILURE);
    }

    /* Events buffer used by epoll_wait to list triggered events */
    events = (struct epoll_event*) calloc (MAX_EVENTS, sizeof(event));

    // Initialize all conn states
    for (int i = 0; i < MAX_STATES; i++) {
        init_conn_state(&conn_states[i]);
    }
    int conn_state_idx = 0;

    while(1) {
        int num_events = epoll_wait(efd, events, MAX_EVENTS, 1000);
        printf("num_events: %i\n", num_events);
        for (int i = 0; i < num_events; i++) {
            conn_state_t* active_conn_state = (conn_state_t *) events[i].data.ptr;

            // Skip over active event if ER, HUP, or RDHUP
            if ((events[i].events & EPOLLERR) ||
                    (events[i].events & EPOLLHUP) ||
                    (events[i].events & EPOLLRDHUP)) {
                perror("epoll error");
                continue;
            }

            // When client wants to connect to proxy
            else if (active_conn_state->client_socket_fd == listenfd) {

                // Initialize active event
                active_conn_state = &conn_states[conn_state_idx];
                conn_state_idx += 1;
                if (conn_state_idx >= MAX_STATES)
                    conn_state_idx = 0;

                // Connect to client
                connect_to_client(listenfd, active_conn_state, efd);    
            }

            // Every other type of connection
            else {
                switch (active_conn_state->state) {
                    // 1.  Client -> Proxy
                    case STATE_READ_REQ:
                        read_request(active_conn_state, efd);
                        break;

                    // 2.  Proxy -> Server
                    case STATE_SEND_REQ:
                        send_request(active_conn_state, efd);
                        break;

                    // 3.  Server -> Proxy
                    case STATE_READ_RES:
                        read_response(active_conn_state, efd);
                        break;

                    // 4.  Proxy -> Client
                    case STATE_SEND_RES:
                        send_response(active_conn_state, efd);
                        init_conn_state(active_conn_state);
                        break;
                }
            }
        }


    }

    // Cleanup
    free(events);

    return 0;
}