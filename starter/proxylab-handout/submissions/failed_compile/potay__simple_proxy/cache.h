#include <stdlib.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400



typedef struct cache_block{
    size_t size;
    char *data;
    char *tag;
    int timestamp;
    sem_t mutex;
    struct cache_block* next;
} cache_block;

typedef struct cache_header{
    size_t total_size;
    cache_block* start;
    int currTime;
} cache_header;

struct cache_header* cache_init();
int cache_empty(struct cache_header *cache);
void remove_from_cache(struct cache_header *cache, size_t size);
void insert_into_cache(struct cache_header *cache, size_t size, char* data, char* tag);
struct cache_block* find_in_cache(struct cache_header *cache, char* tag);


