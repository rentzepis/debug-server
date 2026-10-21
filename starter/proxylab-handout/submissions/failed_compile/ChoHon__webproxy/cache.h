#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_t
{
    char url[MAXLINE], response_header[MAXLINE], response_body[MAXLINE];
    int filesize;
    struct cache_t *prev, *next;
} cache_t;

typedef struct
{
    cache_t *head;
    int current_cache_size, readcnt;
    sem_t w, mutex;
} cache_header;

cache_header *cache_init();
int cache_append(cache_header *ch, char *url, char *response_header, char *response_body, int filesize);
void cache_delete_LRU(cache_header *ch);
cache_t *cache_search(cache_header *ch, char *url);
void cache_display(cache_t *cache);
void cache_all_display(cache_header *ch);