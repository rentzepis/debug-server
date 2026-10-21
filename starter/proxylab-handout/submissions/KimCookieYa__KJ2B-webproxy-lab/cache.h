#include <stdio.h>

#include "csapp.h"

typedef struct web_object_t {
    char path[MAXLINE];
    int content_length;
    char *response_ptr;
    struct web_object_t *prev, *next;
} web_object_t;

web_object_t *find_cache(char *path);
void send_cache(web_object_t *web_object, int clientfd);
void read_cache(web_object_t *web_object);
void write_cache(web_object_t *web_object);

extern web_object_t *rootp;
extern web_object_t *lastp;
extern int total_cache_size;

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400