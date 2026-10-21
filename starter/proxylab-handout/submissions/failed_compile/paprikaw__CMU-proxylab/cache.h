#include "proxy.h"
#include <stddef.h>
#define MAX_HASH_LINE 1024
struct cache_node
{
    struct cache_node *prev;
    struct cache_node *next;
    unsigned long hash;
    size_t buf_size;
    char buf[MAX_OBJECT_SIZE];
};
typedef struct cache_node cache_node_t;

typedef struct
{
    cache_node_t *head;
    cache_node_t *tail;
    size_t length;
    size_t max_length;
} cache_t;

int init_cache(size_t cache_size, cache_t *cache);
cache_node_t *lookup_cache(cache_t *cache, char *url);
void insert_cache(cache_t *cache, char *buf, size_t buf_size);