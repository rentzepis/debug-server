/**
 * @file cache.c
 * @brief Implementation of a simple cache system for storing web content.
 *
 * This file defines functions to initialize, manage, and interact with a cache
 * that stores web content in a least recently used (LRU) manner. The cache is
 * designed to be thread-safe, utilizing a global mutex to synchronize access.
 *
 * The cache supports operations such as initialization, deinitialization,
 * finding entries by URL, adding new entries, and evicting the least recently
 * used entry when the cache is full. Each cache entry consists of a URL, the
 * content of the web page, its size, and a reference count to manage usage.
 *
 * This implementation is suitable for proxy servers or similar applications
 * where a cache is used to store and manage frequently accessed web content,
 * optimizing the retrieval of resources by serving them from the cache when available.
 *
 */
#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>

// Define the structure for a cache entry
typedef struct CacheEntry {
    char *url;
    char *object;
    size_t object_size;
    int ref_count;
    struct CacheEntry *prev, *next;
} CacheEntry;

// Define the structure for the cache
typedef struct {
    CacheEntry *head;
    CacheEntry *tail;
    size_t current_cache_size;
} Cache;

void cache_init(Cache *cache);
void cache_deinit(Cache *cache);
CacheEntry *cache_find(Cache *cache, char *url);
void cache_add(Cache *cache, char *url, char *object, size_t object_size);
void cache_evict(Cache *cache);

#endif // CACHE_H
