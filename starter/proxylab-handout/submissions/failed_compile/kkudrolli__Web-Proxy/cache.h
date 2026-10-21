/*
 * cache.h
 *
 * Author: Kais Kudrolli
 * Andrew ID: kkudroll
 *
 * File Description: This is the header file for cache.c, which contains
 * all the main cache functions and their helper functions. This file
 * just has the relevant macros, global variables, structure definitions,
 * and function prototypes.
 *
 */

/* Include guards */
#ifndef __CACHE_H__
#define __CACHE_H__

#include <string.h>

#include "csapp.h"

/* Macros */
#define MAX_CACHE_SIZE  1049000 /* Max size of the entire cache */
#define MAX_OBJECT_SIZE 102400  /* Max size of one cache object */

/* Global variables */
extern pthread_rwlock_t cache_lock; /* Lock for global cache,
                                       defined in proxy.c */

/*
 * Defines a node in the cache, which is implemented as
 * a doubly-linked list.
 */
typedef struct Cache {
    int object_size;               /* Size of cache obj stored at this node */
    int lru_count;                 /* Counter to keep track of LRU node */
    char uri[MAXLINE];             /* URI used as key to find content in 
                                      cache */
    char content[MAX_OBJECT_SIZE]; /* The actual content from the web server */
    struct Cache *next;            /* Pointer to next node in cache */
    struct Cache *prev;            /* Pointer to previous node in cache */
} Cache;

/* Main Cache Function Prototpyes */
Cache *cache_init(void);
int cache_lookup(Cache *cache, char *uri, char *content);
void cache_add(Cache *cache, char *uri, char *content);
void cache_destroy(Cache *cache);
/* Cache Helper Functions */
int get_cache_size(Cache *cache);
void add_node(Cache *cache, char *uri, char *content, int object_size);
void remove_node(Cache *cache, int remove_size);
void print_cache(Cache *cache);
/* Pthread Warning Wrapper Functions */
int Pthread_rwlock_init(pthread_rwlock_t *rwlock, 
        const pthread_rwlockattr_t *attr);
int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);
int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);
int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock);

#endif
