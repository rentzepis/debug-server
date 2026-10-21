
#include <pthread.h>
#include <stddef.h>

#ifndef CACHE_H
#define CACHE_H

#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_CACHE_SIZE (1024 * 1024)

typedef struct cache_obj {
    char *key; // uri
    char *web_obj;
    size_t size; // bytes of web_obj
    int reference_cnt;
    struct cache_obj *prev;
    struct cache_obj *next;
} cache_obj_t;

/*
 * init global cache for storing web objects
 */
void init_cache();

/*
 * insert a web obecjt to cache
 */
void insert_cache_obj_to_cache(char *key, size_t size, char *web_obj);

/*
 * search if a uri request had been cached by passing it as a key
 * if hit: return the cache_obj
 * else miss: return NULL
 */
cache_obj_t *search_cache_obj(const char *key);

/*
 * free a cache_obj whicin was in use in the cache
 */
void free_cache_obj(cache_obj_t *obj);
#endif