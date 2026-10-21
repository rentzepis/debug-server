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
 */
#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)
// Global mutex for synchronizing access to the cache
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

/**
 * @brief Initializes the cache structure.
 *
 * @param cache Pointer to the Cache structure to be initialized.
 *
 * This function initializes the cache structure, setting its head and tail pointers to NULL,
 * and initializing the current cache size to zero.
 */
void cache_init(Cache *cache) {
    cache->head = NULL;
    cache->tail = NULL;
    cache->current_cache_size = 0;
}

/**
 * @brief Cleans up and deinitializes the cache structure.
 *
 * @param cache Pointer to the Cache structure to be deinitialized.
 *
 * This function releases the memory allocated for each cache entry and the cache structure itself.
 */
void cache_deinit(Cache *cache) {
    CacheEntry *entry = cache->head;
    while (entry != NULL) {
        CacheEntry *next_entry = entry->next;
        free(entry->url);
        free(entry->object);
        free(entry);
        entry = next_entry;
    }
}

/**
 * @brief Finds a cache entry by URL.
 *
 * @param cache Pointer to the Cache structure.
 * @param url URL of the cache entry to find.
 * @return Pointer to the found CacheEntry or NULL if not found.
 *
 * This function searches the cache for an entry with the specified URL.
 * If found, it moves the entry to the head of the cache list, marking it as recently used.
 */
CacheEntry *cache_find(Cache *cache, char *url) {
    pthread_mutex_lock(&cache_mutex);
    CacheEntry *entry = cache->head;
    while (entry != NULL) {
        if (strcmp(entry->url, url) == 0) {
            // Move the entry to the head of the list to mark it as recently used
            if (entry != cache->head) {
                // Remove entry from its current position
                if (entry->prev) entry->prev->next = entry->next;
                if (entry->next) entry->next->prev = entry->prev;
                if (entry == cache->tail) cache->tail = entry->prev;
                // Insert entry at the head of the list
                entry->next = cache->head;
                entry->prev = NULL;
                if (cache->head) cache->head->prev = entry;
                cache->head = entry;
            }
            pthread_mutex_unlock(&cache_mutex);
            return entry;
        }
        entry = entry->next;
    }
    pthread_mutex_unlock(&cache_mutex);
    return NULL;
}

/**
 * @brief Adds a new entry to the cache.
 *
 * @param cache Pointer to the Cache structure.
 * @param url URL of the new cache entry.
 * @param object Content of the new cache entry.
 * @param object_size Size of the content in bytes.
 *
 * This function adds a new cache entry to the cache. If the cache is full,
 * it may trigger the eviction of the least recently used entry to make room for the new entry.
 */
void cache_add(Cache *cache, char *url, char *object, size_t object_size) {
    pthread_mutex_lock(&cache_mutex);
    // Evict objects if the cache is full or the new object is too large
    while (cache->current_cache_size + object_size > MAX_CACHE_SIZE) {
        if (cache->current_cache_size == 0 || object_size > MAX_OBJECT_SIZE) {
            // If the cache is empty or the object is too large, don't add it
            pthread_mutex_unlock(&cache_mutex);
            return;
        }
        // Call cache_evict to remove the least recently used object
        printf("run into evict\n");
        pthread_mutex_unlock(&cache_mutex);
        cache_evict(cache);
        pthread_mutex_lock(&cache_mutex);
    }

    // Allocate memory for the new cache entry
    CacheEntry *new_entry = malloc(sizeof(CacheEntry));
    if (new_entry == NULL) {
        pthread_mutex_unlock(&cache_mutex);
        return; // Failed to allocate memory for the new entry
    }
    new_entry->url = strdup(url);
    new_entry->object = malloc(object_size);
    if (new_entry->object == NULL) {
        free(new_entry->url);
        free(new_entry);
        pthread_mutex_unlock(&cache_mutex);
        return; // Failed to allocate memory for the object
    }
    memcpy(new_entry->object, object, object_size);
    new_entry->object_size = object_size;
    new_entry->ref_count = 1; // Initial reference count

    // Add the new entry to the head of the cache list
    new_entry->next = cache->head;
    new_entry->prev = NULL;
    if (cache->head != NULL) {
        cache->head->prev = new_entry;
    }
    cache->head = new_entry;
    if (cache->tail == NULL) {
        cache->tail = new_entry;
    }
    cache->current_cache_size += object_size;
    printf("Cache add: URL=%s, size=%zu, current cache size=%zu\n", url, object_size, cache->current_cache_size);
    pthread_mutex_unlock(&cache_mutex);
}


/**
 * @brief Evicts the least recently used entry from the cache.
 *
 * @param cache Pointer to the Cache structure.
 *
 * This function removes the least recently used entry from the cache if the cache is full.
 * It frees the memory occupied by the evicted entry and updates the cache size accordingly.
 */
void cache_evict(Cache *cache) {
    printf("Evict: Debug1\n");
    pthread_mutex_lock(&cache_mutex);
    printf("Evict: Debug2\n");
    
    while (cache->current_cache_size > MAX_CACHE_SIZE - MAX_OBJECT_SIZE) {
        CacheEntry *entry = cache->tail;
        if (entry == NULL) {
            pthread_mutex_unlock(&cache_mutex);
            printf("Evict: Cache is empty\n");
            break; // Cache is empty
        }
        printf("Cache evict: URL=%s, size=%zu, current cache size=%zu\n", cache->tail->url, cache->tail->object_size, cache->current_cache_size);
        // Remove the entry from the cache list
        if (entry->prev) {
            entry->prev->next = NULL;
        }else {
            // If this is the only entry, update the head as well
            cache->head = NULL;
        }
        cache->tail = entry->prev;
        cache->current_cache_size -= entry->object_size;
        // Free the entry's memory
        free(entry->url);
        free(entry->object);
        free(entry);
        printf("Evict: Debug3\n");
    }
    
    pthread_mutex_unlock(&cache_mutex);
}
