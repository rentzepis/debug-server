#ifndef CACHE_H
#define CACHE_H

#include "csapp.h"

/* Cache structure */
typedef struct cache_node {
    char *url;
    char *content;
    size_t content_size;
    struct cache_node *prev;
    struct cache_node *next;
} cache_node_t;

typedef struct {
    cache_node_t *head;
    cache_node_t *tail;
    size_t total_size;
    sem_t mutex;
    sem_t w;  // Writer lock for cache
    int readcnt;  // Number of readers
    sem_t readcnt_mutex;  // Mutex for readcnt
} cache_t;

/* Cache functions */
void cache_init(cache_t *cache);
void cache_deinit(cache_t *cache);
cache_node_t *cache_find(cache_t *cache, const char *url);
void cache_insert(cache_t *cache, const char *url, const char *content, size_t content_size);
void cache_evict_lru(cache_t *cache);

#endif /* CACHE_H */


