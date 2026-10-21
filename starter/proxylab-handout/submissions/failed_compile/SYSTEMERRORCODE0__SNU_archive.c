#include <stdio.h>
#include "csapp.h"
#include <semaphore.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

/* semaphores */
sem_t mutex, w;
int readcnt;

/* cache struct */
struct cache_data {
    char *url;
    char *response;
    int response_length;
    char *content;
    int content_length;
    struct cache_data *prev;
    struct cache_data *next;
};

struct cache_data *head = NULL, *tail = NULL; // doubly linked list
int total_cache_size = 0;


/*
 *  cache functions
 */
 
// delete from list, not free
void delete(struct cache_data *cache) {
    if(cache->prev == NULL) head = cache->next;
    else cache->prev->next = cache->next;
    
    if(cache->next == NULL) tail = cache->prev;
    else cache->next->prev = cache->prev;
    
    total_cache_size -= (cache->response_length + cache->content_length);
}

// insert to head of list
void insert_head(struct cache_data *cache) {
    if(head == NULL) tail = cache;
    else head->prev = cache;
    cache->next = head;
    cache->prev = NULL;
    head = cache;
    
    total_cache_size += (cache->response_length + cache->content_length);
}

// referenced solution of 1st Readers-Writers Problem
struct cache_data *find(char *input_url) {
    sem_wait(&mutex);	// don't use P/V for avoiding termination
    readcnt++;
    if(readcnt == 1) sem_wait(&w);
    sem_post(&mutex);
    
    // Reading
    struct cache_data *now = head;
    while(now != NULL) {
        if(!strcmp(now->url, input_url)) { // cache found
            delete(now);
            insert_head(now);
            break;
        }
        now = now->next;
    }
    
    sem_wait(&mutex);
    readcnt--;
    if(readcnt == 0) sem_post(&w);
    sem_post(&mutex);
    
    return now;
}

void *add_cache(char *url, char *response, int response_length, char *content, int content_length) {

    // Check content length
    if(response_length + content_length > MAX_OBJECT_SIZE) {
        Free(content);
        return;
    }

    sem_wait(&w);
    
    // Make space for new cache
    while(total_cache_size + response_length + content_length > MAX_CACHE_SIZE) {
    	struct cache_data *tail_cache = tail;
        delete(tail_cache);
        Free(tail_cache->url);
        Free(tail_cache->response);
        Free(tail_cache->content);
        Free(tail_cache);
    }

    // allocate memory for cache, content already allocated.
    struct cache_data *cache = (struct cache_data *)malloc(sizeof(struct cache_data));

    cache->url = malloc(strlen(url)+1);
    strcpy(cache->url, url);

    cache->response = malloc(response_length + 1);
    strcpy(cache->response, response);

    cache->response_length = response_length;
    cache->content = content;
    cache->content_length = content_length;
    
    insert_head(cache);
    
    sem_post(&w);
    
    return;
}



// transmit data to client
void transmit(int connect_fd) {
    
    /*
     *  Get request header
     */

    // initialize Robust I/O from client
    rio_t rio_client;
    Rio_readinitb(&rio_client, connect_fd);

    // Input request header
    char input_client[MAXLINE]; // MAX 8192 bytes, declared in csapp.h
    if(rio_readlineb(&rio_client, input_client, MAXLINE) < 0) {
    	fprintf(stderr, "rio_readlineb error\n");
    	return;
    }
    printf("Recieved request header : %s",input_client);
    
    // Parse request header
    char command[MAXLINE], url[MAXLINE], http_version[MAXLINE];
    sscanf(input_client, "%s %s %s", command, url, http_version); // will not use command, http_version

    // Drop remaining request header
    char temp[MAXLINE];
    do {
        if(rio_readlineb(&rio_client, temp, MAXLINE) < 0) {
    	    fprintf(stderr, "rio_readlineb error\n");
    	    return;
        }
    } while(strcmp(temp, "\r\n"));

    /*
     *  Check request header
     */ 

    // Check command
    if(strcasecmp(command, "GET")) { // non-zero means different. strcasecmp() : compare not considering Upper/Lowercase
        fprintf(stderr, "Not supporting command\n");
        return;
    }

    // Parse url
    char host[MAXLINE], filename[MAXLINE];
    int error = sscanf(url, "http://%[^/]/%s", host, filename); // host = host + port now
    if(error <= 0) return;

    // Get port
    char *port_ptr = strstr(host, ":");
    int port;
    
    if(port_ptr == NULL) port = 80;
    else {
        *port_ptr = 0; // for seperate host and port, host = host now
        port_ptr++;
        port = atoi(port_ptr);
        if(port <= 0) return;   // need port, but not allowed port
    }



    /*
     *  Check cache, if exist, write.
     */
    
    struct cache_data *cache = find(url);
    if(cache != NULL) {
        rio_writen(connect_fd, cache->response, cache->response_length);
        rio_writen(connect_fd, cache->content, cache->content_length);
        return;
    }
     
     

    /*
     *  Send request to host
     */ 
    
    // Try re-connect to server if fail
    int server_fd, attempt = 0;

    while((server_fd = open_clientfd(host, port_ptr)) < 0) {
        if(attempt > 20) return;
        attempt++;
    }

    // Send refined request : GET /(filename) HTTP/1.0 fixed
    char output[MAXLINE], output_length;
    
    sprintf(output, "GET /%s HTTP/1.0", filename);
    output_length = strlen(output);
    if(rio_writen(server_fd, output, output_length) != output_length) {
    	fprintf(stderr, "rio_writen error\n");
    	return;
    }
    
    sprintf(output, "Host: %s:%d\r\n\r\n", host, port);
    output_length = strlen(output);
    if(rio_writen(server_fd, output, output_length) != output_length) {
    	fprintf(stderr, "rio_writen error\n");
    	return;
    }



    /*
     *  Get & Send response header
     */

    // initialize Robust I/O from server
    rio_t rio_server;
    Rio_readinitb(&rio_server, server_fd);

    char input_server[MAXLINE] = {};
    char response[MAXLINE] = {};
    int is_chunked = 0;
    int content_length = 0;
    do {
        if(rio_readlineb(&rio_server, input_server, MAXLINE) < 0) {
    	    fprintf(stderr, "rio_readlineb error\n");
    	    return;
        }
        // use strncasecmp for check front 18 bytes & check Chunked
        if(!strncasecmp(input_server, "Transfer-Encoding:", 18) && 
            (strstr(input_server, "chunked") != NULL || strstr(input_server, "Chunked") != NULL)) {
            is_chunked = 1;
        } else if(!strncasecmp(input_server, "Content-Length:", 15)) {
            sscanf(input_server + 15, "%d\r\n", &content_length);
        }

        strcat(response, input_server);

    } while(strcmp(input_server, "\r\n"));

    output_length = strlen(response);
    if(rio_writen(connect_fd, response, output_length) != output_length) {
    	fprintf(stderr, "rio_writen error\n");
    	return;
    }

    /*
     *  Get & Send response body
     */
    char *content;
    if(is_chunked) {

        // initialize content using first chunk
        char *chunk_length_input;
        if(rio_readlineb(&rio_server, chunk_length_input, MAXLINE) < 0) {
    	    fprintf(stderr, "rio_readlineb error\n");
    	    return;
        }

        content_length = strlen(chunk_length_input) + 2;  // add 2 for "\r\n" at the end
        content = (char *)malloc(content_length);
        strcpy(content, chunk_length_input);

        int chunk_length;
        sscanf(chunk_length_input, "%x\r\n", &chunk_length);

        while(chunk_length > 0) {
            // get real content
            char *chunk = (char *)malloc(chunk_length + 2);
            if(rio_readnb(&rio_server, chunk, chunk_length) < 0) {
    	        fprintf(stderr, "rio_readnb error\n");
    	        return;
            }
            chunk[chunk_length] = 0;    // end of chunk

            // put the chunk into content
            content_length += chunk_length + 1;
            content = realloc(content, content_length);
            strcat(content, chunk);
            free(chunk);

            // get length of next chunk
            if(rio_readlineb(&rio_server, chunk_length_input, MAXLINE) < 0) {
    	        fprintf(stderr, "rio_readlineb error\n");
    	        return;
            }
            content_length += strlen(chunk_length_input) + 1;

            // put length of next chunk
            content = realloc(content, content_length);
            strcat(content, chunk_length_input);

            // type the length into int
            sscanf(chunk_length_input, "%x\r\n", &chunk_length);
        }
        strcat(content, "\r\n");
    } else {
        content = (char *)malloc(content_length);
        if(rio_readnb(&rio_server, content, content_length) < 0) {
    	    fprintf(stderr, "rio_readnb error\n");
    	    return;
        }
    }

    if(rio_writen(connect_fd, content, content_length) != content_length) {
    	fprintf(stderr, "rio_writen error\n");
    	return;
    }

    add_cache(url, response, output_length, content, content_length); // output_length = response_length

    close(server_fd);
}

void *thread(void *vargp) {
    int connect_fd = *((int *)vargp);
    if(pthread_detach(pthread_self()) != 0) { // make detach mode
    	fprintf(stderr, "pthread_detach error\n");
    	return NULL;
    }
    
    Free(vargp);    // free argument for spare memory

    transmit(connect_fd);
    close(connect_fd);
    return NULL;
}

int main(int argc, char **argv)
{
    printf("%s", user_agent_hdr);

    // command error
    if(argc != 2) {
        fprintf(stderr, "Error in use : ./proxy (port)\n");
        return 1;
    }

    // ignore SIGPIPE first
    signal(SIGPIPE, SIG_IGN);

    int port_number = atoi(argv[1]);

    // check port number : 4500 < port_number < 65000
    if(port_number <= 0) { //|| port_number >= 65000) {
        fprintf(stderr, "Error : port number must be 4500 < port < 65000\n");
        return 1;
    }

    // resources for proxy & connection
    int listen_fd, attempt = 0;
    while((listen_fd = open_listenfd(argv[1])) < 0) {
    	if(attempt > 20) {
    	    fprintf(stderr, "Cannot open listen fd\n");
    	    return 1;
    	}
    	attempt++;
    }
    struct sockaddr_in client_address;
    int client_length = sizeof(client_address);
    int *connect_fd_ptr;
    pthread_t tid;
    
    // initialize semaphores, if error, just terminate in this case
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);

    // keep connecting clients & run thread
    while(1) {
        connect_fd_ptr = malloc(sizeof(int));
        *connect_fd_ptr = accept(listen_fd, (SA *)&client_address, &client_length);
        
        if(*connect_fd_ptr < 0) {
            fprintf(stderr, "Accept Error\n");
            Free(connect_fd_ptr);
            continue;
        }
        
        pthread_create(&tid, NULL, thread, connect_fd_ptr);
    }

    return 0;
}
