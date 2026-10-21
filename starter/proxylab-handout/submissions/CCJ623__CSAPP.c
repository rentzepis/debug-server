#include <stdio.h>
#include "csapp.h"
#include "cache.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define DEBUG_MODE

/* You won't lose style points for including this long line in your code */
static const char* user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static unsigned short PORT;
static Cache cache;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;


void printLog(const char* operation, const char* log);
void* work(void* client_fd_ptr);


int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        app_error("The number of arguments must be 1!\n");
    }
    char* end_ptr;
    PORT = strtol(argv[1], &end_ptr, 10);
    if (*end_ptr != '\0')
    {
        app_error("Port number must be integer!\n");
    }

#ifdef DEBUG_MODE
    printLog("listen port", argv[1]);
#endif

    int listen_fd = Open_listenfd(argv[1]);
    initCache(&cache);
    // wait for client request
    while (1)
    {
        SA client_socket;
        socklen_t client_socket_length = sizeof(client_socket);
        char host_name[MAXLINE];
        int client_fd = Accept(
            listen_fd,
            &client_socket,
            &client_socket_length);
        Getnameinfo(&client_socket, sizeof(client_socket), host_name, MAXLINE, NULL, MAXLINE, 0);

#ifdef DEBUG_MODE
        printLog("Accept", host_name);
#endif
        pthread_t thread_id;
        Pthread_create(&thread_id, NULL, work, &client_fd);
        Pthread_detach(thread_id);
    }

    Close(listen_fd);
    freeCache(&cache);
    return 0;
}

void printLog(const char* operation, const char* log)
{
    printf("%s: %s\n", operation, log);
}

void* work(void* client_fd_ptr)
{
    int client_fd = *(int*)client_fd_ptr;
    char buffer[MAXLINE], * buffer_end = buffer, line_buffer[MAXLINE];
    char method[MAXLINE], url[MAXLINE], version[MAXLINE];
    *buffer = *line_buffer = *method = *url = *version = '\0';
    rio_t rio_client;
    char server_port[MAXLINE] = "80";
    Rio_readinitb(&rio_client, client_fd);
    Rio_readlineb(&rio_client, line_buffer, MAXLINE);
#ifdef DEBUG_MODE
    printLog("Read from client", line_buffer);
#endif
    sscanf(line_buffer, "%s %s %s", method, url, version);
    if (strcasecmp(method, "GET") != 0)
    {
        printf("Can not deal with method: %s\n", method);
        Close(client_fd);
        Pthread_exit(NULL);
    }
    if (strcasecmp(version, "HTTP/1.1") != 0 && strcasecmp(version, "HTTP/1.0") != 0)
    {
        printf("Proxy can only deal with HTTP/1.1 or HTTP/1.0\n");
        Close(client_fd);
        Pthread_exit(NULL);
    }

    // parse url
    char host[MAXLINE], path[MAXLINE];
    char* first = strstr(line_buffer, "://");
    first = (first == NULL ? line_buffer : first + 3);
    char* last = strchr(first, '/');
    strncpy(host, first, last - first);
    host[last - first] = '\0';
    strcpy(url, host);
    first = last, last = strchr(first, ' ');
    strncpy(path, first, last - first);
    path[last - first] = '\0';
    strcat(url, path);

    // found in cache
    pthread_mutex_lock(&mutex);
    CacheEntry* entry = readEntry(&cache, url);
    if (entry != NULL)
    {
        Rio_writen(client_fd, entry->data, entry->data_length);
        pthread_mutex_unlock(&mutex);
#ifdef DEBUG_MODE
        printLog("Found in cache", url);
        printLog("Write to client", "");
#endif
        Close(client_fd);
        Pthread_exit(NULL);
    }
    pthread_mutex_unlock(&mutex);

    // parse server port
    first = strchr(host, ':');
    if (first != NULL)
    {
        strtol(first + 1, &last, 10);
        if (*last != '\0')
        {
            app_error("port must be integer!");
        }
        strcpy(server_port, first + 1);

        // discard port
        *first = '\0';
    }

    // build request line
    strcat(buffer_end, method);
    buffer_end += strlen(method);
    strcat(buffer_end, " ");
    buffer_end += strlen(" ");
    strcat(buffer_end, path);
    buffer_end += strlen(path);
    strcat(buffer_end, " ");
    buffer_end += strlen(" ");
    strcat(buffer_end, "HTTP/1.0\r\n");
    buffer_end += strlen("HTTP/1.0\r\n");

    // add headers
    strcat(buffer, user_agent_hdr);
    strcat(buffer, "Connection: Close\r\n");
    strcat(buffer, "Proxy-Connection: close\r\n");

    // parse headers
    while (1)
    {
        char key[MAXLINE];
        Rio_readlineb(&rio_client, line_buffer, MAXLINE);
#ifdef DEBUG_MODE
        printLog("Read from client", line_buffer);
#endif

        // break if read empty line
        if (strcmp(line_buffer, "\r\n") == 0)
        {
            break;
        }

        strncpy(key, line_buffer, strchr(line_buffer, ':') - line_buffer);

        // skip specific headers
        if (strcasecmp(key, "User-Agent") == 0
            || strcasecmp(key, "Connection") == 0
            || strcasecmp(key, "Proxy-Connection") == 0)
        {
            continue;
        }

        // forward headers
        strcat(buffer, line_buffer);
    }

    // add empty line
    strcat(buffer, "\r\n");

    // forward to server
    int server_fd = Open_clientfd(host, server_port);
    Rio_writen(server_fd, buffer, strlen(buffer));
#ifdef DEBUG_MODE
    printLog("Write to server", buffer);
#endif

    // back to client
    rio_t rio_server;
    Rio_readinitb(&rio_server, server_fd);
#ifdef DEBUG_MODE
    int receive_file_fd = Open("receive_file.txt", O_WRONLY | O_TRUNC, 0);
#endif
    size_t bytes_read = 0, data_buffer_size = 0;
    uint8_t data_buffer[MAX_OBJECT_SIZE];
    while (1)
    {
        bytes_read = Rio_readlineb(&rio_server, line_buffer, MAXLINE);
        if (bytes_read == 0)
        {
            break;
        }
#ifdef DEBUG_MODE
        printLog("Read from server", line_buffer);
#endif
        Rio_writen(client_fd, line_buffer, bytes_read);
        memcpy(data_buffer + data_buffer_size, line_buffer, bytes_read);
        data_buffer_size += bytes_read;
#ifdef DEBUG_MODE
        printLog("Write to client", line_buffer);
        Write(receive_file_fd, line_buffer, bytes_read);
        printLog("Write to receive_file.txt", line_buffer);
#endif
    }

    // caching data
    if (data_buffer_size <= MAX_OBJECT_SIZE)
    {
        entry = createCacheEntry(url, data_buffer, data_buffer_size);
        pthread_mutex_lock(&mutex);
        appendEntry(&cache, entry);
        pthread_mutex_unlock(&mutex);
    }

    // close
    Close(client_fd);
    Close(server_fd);
    Pthread_exit(NULL);
    return NULL;
}

