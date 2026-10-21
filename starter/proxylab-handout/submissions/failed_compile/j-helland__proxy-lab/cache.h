/**
 * @author Jonathan Helland
 *
 * An LRU cache implementation, using a hash table and doubly linked circular
 * list as the underlying data structures.
 */
#ifndef CACHE_H
#define CACHE_H

#include "hashmap.h"
#include "list.h"

#include <pthread.h>
#include <stddef.h>

/**
 * Holds individual entries in the cache along with their metadata.
 *
 * @param  key       The key used for lookup in the hash table.
 * @param  value     The value associated with the key in the hash table.
 * @param  size      The number of bytes consumed by the value.
 * @param  keylen    The number of bytes for the key.
 */
typedef struct Block {
    void *key;
    void *value;
    size_t size;
    size_t keylen;
} block_t;

/**
 * The wrapper for the cache, primarily composed of a hash table to store values
 * and handle fast retrieval, and doubly linked circular list to enforce the LRU
 * eviction policy.
 *
 * @param  map       The hash table.
 * @param  lru_list  The linked list to track usage ordering for the LRU
 *                   eviction policy.
 * @param  size      The number of bytes currently used by the values stored.
 *                   This does not include overhead, including keys, the hash
 *                   table itself, and the LRU list itself.
 * @param  max_size  The largest number of bytes storable in the cache. The size
 *                   will never exceed this.
 */
typedef struct Cache {
    hashmap_t *map;
    list_t *lru_list;
    size_t size, max_size;
} cache_t;

/**
 * Initialize memory for the cache and return a pointer to it.
 * Must be freed later by cache_free.
 */
cache_t *cache_init(size_t size);

/**
 * Free the memory consumed by the cache. This includes memory used by the keys
 * and values themselves.
 */
void cache_free(cache_t *cache);

/**
 * Remove an entry from the cache. This will free the memory of the key and
 * value as well.
 */
int cache_delete(cache_t *cache, block_t *block);

/**
 * Load a new entry into the cache. This will copy the key and value.
 */
int cache_insert(cache_t *cache, const void *key, size_t keylen,
                 const void *value, size_t size);

/**
 * Find and return an entry in the cache. Returns NULL if the entry doesn't
 * exist. This will increment the refcount for the entry -- be sure to call
 * cache_release when finished with the entry.
 */
block_t *cache_find(cache_t *cache, const void *key, size_t keylen);

#endif
