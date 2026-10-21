#ifndef __CACHE_H__
#define __CACHE_H__

#include <stdio.h>
#include <assert.h>
#include "csapp.h"

/* Recommended max cache size */
#define MAX_CACHE_SIZE 1049000

/* How many elements can the cache accommodate initially */
#define CACHE_INITIAL_CAPACITY 4

typedef char* ObjectData;

typedef struct {
    char* uri;
    size_t size;
    ObjectData data;
    int timestamp; // UNIX version
} CacheEntry;

/* Called at start of main function */
void init_cache();

/*
 * Sets the cache entry for the given URI to the current timestamp, if found.
 */
void bump_timestamp_for_cached_uri(char* uri);

/*
 * Returns cached data for URI. If data isn't in the cache, return NULL.
 */
CacheEntry* get_cached_uri_entry(char* uri);

/*
 * Here, we just append to the end, meaning we grow the vector and evict if necessary.
 *
 * ! Guaranteed not to be called if the new cache entry's URL was already found somewhere.
 */
void add_cached_uri_entry(CacheEntry* entry);

#endif /* __CACHE_H__ */
