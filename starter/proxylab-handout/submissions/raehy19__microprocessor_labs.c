#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

///////   Cache   ///////
// Define cache block structure type
typedef struct s_cache_block t_cache_block;

// Cache block structure
typedef struct s_cache_block {
	char path[MAXLINE];
	char *data;
	unsigned size;
	unsigned read;
	t_cache_block *next;
}t_cache_block;

// Cache list head
t_cache_block *head;

// Read count
unsigned read_cnt;

// Semaphores
sem_t sem_r, sem_w;

/////////////////////////

/////// Functions ///////

void *do_proxy(void *p_client_fd);

void parse_req(int fd, char *host, char *port, char *path, char *headers);

void parse_uri(char *uri, char *host, char *port, char *path);

void read_req_hdrs(rio_t *rp, char *headers);

void handle_server_response(int server_fd, int client_fd, char *path);

void write_cache(t_cache_block *new_block);

int search_cache(char *path, int fd);

/////////////////////////

/*
 * main - This function serves as a multithreaded server,
 *        that listens for and accepts client connections,
 *        then assigns each connection to a new thread for ongoing communication
 */
int main(int argc, char **argv) {
	int listen_fd, connect_fd;
	struct sockaddr_storage client_addr;
	socklen_t client_len;

	// Check command line arg
	if (argc != 2) {

		// If argument count is not 2, print usage and exit
		fprintf(stderr, "usage: %s <port>\n", argv[0]);

		exit(1);
	}

	// Init semaphores
	Sem_init(&sem_r, 0, 1);
	Sem_init(&sem_w, 0, 1);

	// Open listening socket
	listen_fd = Open_listenfd(argv[1]);


	while (1) {

		int *p_client_fd;
		pthread_t tid;

		// Set client length
		client_len = sizeof(client_addr);

		// Accept connection from client
		connect_fd = Accept(listen_fd, (SA *)&client_addr, &client_len);

		// Malloc for thread
		p_client_fd = (int *)Malloc(sizeof(connect_fd));

		// Assign connect_fd to p_client_fd
		*p_client_fd = connect_fd;

		// Create thread
		Pthread_create(&tid, NULL, do_proxy, (void *)p_client_fd);
	}
	return 0;
}

/*
 * do_proxy - This function handles client requests,
 *            checks for cached content,
 *            communicates with the server,
 *            and relays server responses to the client.
 */
void *do_proxy(void *p_client_fd) {

	int server_fd, client_fd;
	char host[MAXLINE], port[MAXLINE], path[MAXLINE], headers[MAXLINE];
	char request_line[MAXLINE]; // First line of request

	// Get client_fd from p_client_fd
	client_fd = *(int *)p_client_fd;

	// Free p_client_fd
	Free(p_client_fd);

	// Detach the thread to free up resources once it terminates
	Pthread_detach(Pthread_self());

	// Parse request from client
	parse_req(client_fd, host, port, path, headers);

	// If the requested path is in cache, send it to client
	if (search_cache(path, client_fd)) {

		// Close connection with client
		Close(client_fd);

		return NULL;
	}

	// Prepare GET request line
	sprintf(request_line, "GET %s HTTP/1.0\r\n", path);

	// Open connection with server
	server_fd = Open_clientfd(host, port);

	// Write request line and headers to server
	Rio_writen(server_fd, request_line, strlen(request_line));
	Rio_writen(server_fd, headers, strlen(headers));

	// Receive response from server and send it to client
	handle_server_response(server_fd, client_fd, path);

	// Close connection with client
	Close(client_fd);

	return NULL;
}

/*
 * parse_req - This function parses the request from the client,
 *             and modifies the request line and headers.
 */
void parse_req(int fd, char *host, char *port, char *path, char *headers) {
	char buf[MAXLINE], method[MAXLINE], uri[MAXLINE];
	rio_t rio;

	// Initialize the robust I/O structure
	Rio_readinitb(&rio, fd);

	// Read the request line from the client
	if (!Rio_readlineb(&rio, buf, MAXLINE))
		return; // If the request line is empty, return

	// If the rea
	sscanf(buf, "%s %s", method, uri);

	parse_uri(uri, host, port, path);
	read_req_hdrs(&rio, headers);
}

/*
 * parse_uri - This function parses the URI from the request line,
 *             and modifies the host, port, and path.
 */
void parse_uri(char *uri, char *host, char *port, char *path) {
	char *p_uri;
	char *p_port;
	char *p_path;

	// Initialize p_uri to uri
	p_uri = uri;

	// Check if path starts with http://
	if (strstr(uri, "http://") == uri) {
		// If so, set p_uri to start of path
		p_uri = uri + 7;
	}

	// Check if p_uri contains a path
	p_path = strchr(p_uri, '/');
	if (p_path) {
		// If so, set path
		strcpy(path, p_path);
		p_path[0] = '\0';
	} else {
		// If not, set path to root
		strcpy(path, "/");
	}

	// Check if p_uri contains a port
	p_port = strchr(p_uri, ':');
	if (p_port) {
		// If so, set port
		strcpy(port, p_port + 1);
		p_port[0] = '\0';
	} else {
		// If not, set port to 80
		strcpy(port, "80");
	}

	// Set host
	strcpy(host, p_uri);
}

/*
 * read_req_hdrs - This function reads the request headers from the client,
 *                 and modifies the headers.
 */
void read_req_hdrs(rio_t *rp, char *headers) {
	char buf[MAXLINE] = {0};
	char *p;
	int usr_agent_flag = 0, con_flag = 0, pxy_con_flag = 0;

	// Set headers to empty string
	headers[0] = '\0';

	// Read headers
	while (strcmp(buf, "\r\n")) {
		Rio_readlineb(rp, buf, MAXLINE);

		p = strchr(buf, ':');

		// If p is not NULL, set *p to '\0'
		if (p) {
			*p = '\0';

			// Check if header is User-Agent
			if (!usr_agent_flag && strcasecmp(buf, "User-Agent") == 0) {
				usr_agent_flag = 1;
				strcpy(p + 2, user_agent_hdr);
			}
				// Check if header is Connection
			else if (!con_flag && strcasecmp(buf, "Connection") == 0) {
				con_flag = 1;
				strcpy(p + 1, " close\r\n");
			}
				// Check if header is Proxy-Connection
			else if (!pxy_con_flag && strcasecmp(buf, "Proxy-Connection") == 0) {
				pxy_con_flag = 1;
				strcpy(p + 1, " close\r\n");
			}

			// Restore ':'
			*p = ':';
		}

		strcat(headers, buf);
	}

	// Add User-Agent if not exist
	if (!usr_agent_flag) {
		strcat(headers, "User-Agent: ");
		strcat(headers, user_agent_hdr);
	}

	// Add Connection if not exist
	if (!con_flag) {
		strcat(headers, "Connection: close\r\n");
	}

	// Add Proxy-Connection if not exist
	if (!pxy_con_flag) {
		strcat(headers, "Proxy-Connection: close\r\n");
	}

	// Add end of header
	strcat(headers, buf);
}

/*
 * handle_server_response - This function reads the server's response,
 *                          caches it if its size is within the limit,
 *                          and then forwards the response to the client.
 */
void handle_server_response(int server_fd, int client_fd, char *path) {
	char buf[MAXLINE], header[MAXLINE];
	char *response_body;
	char *start_ptr, *end_ptr;
	unsigned content_length, header_length;
	rio_t rio;

	// Initialize the robust I/O structure
	Rio_readinitb(&rio, server_fd);
	header_length = 0;
	header[0] = '\0';

	// Read the response header from the server
	do {
		Rio_readlineb(&rio, buf, MAXLINE);
		strcat(header, buf);

		// If the "Content-length" header is found, get the content length
		if (strncasecmp(buf, "Content-length:", 15) == 0) {
			start_ptr = buf + 16;
			end_ptr = strchr(start_ptr, '\r');
			*end_ptr = '\0';
			content_length = atoi(start_ptr);
			*end_ptr = '\r';
		}

		// Add the length of the response header to header_length
		header_length += strlen(buf);

		// Write the response header to the client
		Rio_writen(client_fd, buf, strlen(buf));
	} while (strcmp(buf, "\r\n"));

	// Allocate memory for the response body and read it from the server
	response_body = (char *)Malloc(content_length);
	Rio_readnb(&rio, response_body, content_length);

	// If the total size of the response header and body is less than or equal to the maximum object size,
	// then store the response in the cache
	if (header_length + content_length <= MAX_OBJECT_SIZE) {
		t_cache_block *new_cache_block = (t_cache_block *)Malloc(sizeof(t_cache_block));
		new_cache_block->size = header_length + content_length;
		new_cache_block->data = (char *)Malloc(new_cache_block->size);

		// Copy the response header and body to the cache block
		memcpy(new_cache_block->data, header, header_length);
		memcpy(new_cache_block->data + header_length, response_body, content_length);

		// Set the URI and next pointer of the cache block
		strcpy(new_cache_block->path, path);
		new_cache_block->next = NULL;

		// Write the cache block to the cache
		write_cache(new_cache_block);
	}

	// Close the connection with the server
	Close(server_fd);

	// Write the response body to the client
	Rio_writen(client_fd, response_body, content_length);

	// Free the memory allocated for the response body
	Free(response_body);
}

/*
 * write_cache - This function adds a new cache block to the front of the cache list,
 *               and removes blocks from the end if the total cache size exceeds the maximum limit.
 */
void write_cache(t_cache_block *new_block) {
	t_cache_block *curr_block, *prev_block;
	unsigned total_size;

	// Initialize total cache size
	total_size = 0;

	// Acquire write semaphore
	P(&sem_w);

	// Insert new cache block to the front
	new_block->next = head;
	head = new_block;

	// Check if total cache size exceeds MAX_CACHE_SIZE
	curr_block = head;
	while (curr_block) {

		// Calculate total cache size
		total_size += curr_block->size;

		// If total cache size exceeds MAX_CACHE_SIZE, remove blocks
		if (total_size > MAX_CACHE_SIZE) {
			while (curr_block) {
				curr_block = curr_block->next;
				Free((prev_block->next)->data);
				Free(prev_block->next);
			}
			break;
		}

		// Move to next block
		prev_block = curr_block;
		curr_block = curr_block->next;
	}

	// Release write semaphore
	V(&sem_w);
}

/*
 * search_cache - This function searches for a specific path in the cache,
 *                sends the data to the client if found (cache hit),
 *                and reorders the cache list by moving recently read blocks to the front.
 */
int search_cache(char *path, int fd) {
	t_cache_block *curr_block, *prev_block;

	// Set curr_block to head
	curr_block = head;

	// Acquire read semaphore
	P(&sem_r);

	// Increase read count
	++read_cnt;

	// If this thread is the first reader
	if (read_cnt == 1) {

		// Acquire write semaphore
		P(&sem_w);

		// Set read flag of all blocks to 0
		while (curr_block) {
			curr_block->read = 0;
			curr_block = curr_block->next;
		}
	}

	// Release read semaphore
	V(&sem_r);

	// Set curr_block to head
	curr_block = head;

	// Search cache
	while (curr_block) {
		if (strcmp(path, curr_block->path) == 0) {
			curr_block->read = 1;
			Rio_writen(fd, curr_block->data, curr_block->size);
			break;
		}
		curr_block = curr_block->next;
	}

	// Acquire read semaphore
	P(&sem_r);

	// Decrease read count
	--read_cnt;

	// If this thread is the last reader
	if (read_cnt == 0) {

		// Set curr_block to head
		curr_block = head;

		// Find blocks with read flag 1 and move them to the front
		while (curr_block) {
			if (curr_block->read == 1 && curr_block != head) {
				prev_block->next = curr_block->next;
				curr_block->next = head;
				head = curr_block;
				curr_block = prev_block->next;
				continue;
			}

			// If curr_block is head, set prev_block to head
			if (curr_block == head) {
				prev_block = head;
				curr_block = curr_block->next;
			}
				// Else, move to next block
			else {
				curr_block = curr_block->next;
				prev_block = prev_block->next;
			}
		}
		// Release write semaphore
		V(&sem_w);
	}
	// Release read semaphore
	V(&sem_r);

	// Return 1 if cache hit, 0 if cache miss
	if (curr_block)
		return 1;
	return 0;;
}

