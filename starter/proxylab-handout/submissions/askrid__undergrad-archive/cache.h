#ifndef __CACHE_H__
#define __CACHE_H__

#include <string.h>
#include <stddef.h>
#include <stdlib.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CacheNode_
{
    char *url;
    char *data;
    size_t size;
    struct CacheNode_ *prev;
    struct CacheNode_ *next;
} CacheNode;

/* Initiate cache. */
CacheNode *init_cache();

/* Cache new web object. */
void cache_obj(char *url, char *data, size_t size, CacheNode *cache);

/* Evict least recently used object from the cache. */
void evict(CacheNode *cache);

/* Find an object in the cache. */
CacheNode *find(char *url, CacheNode *cache);

/* Move the object to the front of the cache. */
void to_front(char *url, CacheNode *cache);

/* Destruct the cache. */
void destruct_cache(CacheNode *cache);

#endif