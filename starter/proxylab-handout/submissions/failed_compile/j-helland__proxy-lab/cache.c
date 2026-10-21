/**
 * @author Jonathan Helland
 *
 * An LRU cache implementation, using a hash table and doubly linked circular
 * list as the underlying data structures.
 */
#include "cache.h"
#include "csapp.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // memcpy

/**
 * @brief Initialize memory for the cache and return a pointer to it. Must be
 * freed later by cache_free.
 *
 * @param  size  The largest number of bytes (values only) allowed in the cache
 *               at any given time.
 *
 * @return Pointer to the initialized cache.
 */
cache_t *cache_init(size_t size) {
    cache_t *cache = malloc(sizeof(cache_t));
    if (cache == NULL)
        return NULL;

    cache->max_size = size;
    cache->size = 0;

    cache->map = hashmap_init(1);
    cache->lru_list = list_init();

    return cache;
}

/**
 * Free the memory associated with a block, including the key and value.
 */
static void free_block(block_t *block) {
    free(block->key);
    free(block->value);
    free(block);
}

/**
 * Free the memory consumed by the cache. This includes memory used by the keys
 * and values themselves.
 */
void cache_free(cache_t *cache) {
    node_t *node = cache->lru_list->head;
    for (bool looped = false; node != NULL && !looped;) {
        block_t *block = node->value;
        free_block(block);

        node = node->next;
        looped = (node == cache->lru_list->head);
    }

    list_free(cache->lru_list);
    hashmap_free(cache->map);
    free(cache);
}

/**
 * Remove an entry from the cache. This will free the memory of the key and
 * value as well.
 *
 * @return 0 if successfully removed.
 * @return -1 if the entry does not exist in the cache.
 */
int cache_delete(cache_t *cache, block_t *block) {
    void *value = hashmap_delete(cache->map, block->key, block->keylen);
    if (value == NULL)
        return -1;

    cache->size -= block->size;
    node_t *node = list_find(cache->lru_list, block);
    list_delete(cache->lru_list, node);
    free(node);
    free_block(block);
    return 0;
}

/**
 * Create a new block given the data to be stored. This must be freed later by
 * free_block.
 *
 * The key and value are copied.
 *
 * @param  key     Bytestring hashed by the hashtable.
 * @param  keylen  Number of bytes to hash.
 * @param  value   Value associated with the key in the hashtable.
 * @param  size    Number of bytes for the value.
 *
 * @return Pointer to the initialized block.
 */
static block_t *get_block(const void *key, size_t keylen, const void *value,
                          size_t size) {
    block_t *block = malloc(sizeof(block_t));

    block->key = malloc(keylen);
    block->key = memcpy(block->key, key, keylen);
    block->keylen = keylen;

    block->value = malloc(size);
    memcpy(block->value, value, size);
    block->size = size;

    return block;
}

/**
 * Load a new block into the cache. Note that the key and value are copied,
 * making them safe to free after insertion.
 *
 * @param  cache   Pointer to cache to insert block into.
 * @param  key     Bytestring hashed by the hashtable.
 * @param  keylen  Number of bytes for the key.
 * @param  value   Value associated with the key in the hashtable.
 * @param  size    Number of bytes for the value.
 * @param  cond    pthread_cond_t used for synchronizing eviction of cache
 *                 blocks.
 * @param  mut     Mutex used for locking shared resources when multithreading.
 *
 * @return 0 if insertion was successful or block already exists in the cache.
 * @return -1 if block could not be inserted due to exceeding the maximum size
 *         of the cache itself.
 */
int cache_insert(cache_t *cache, const void *key, size_t keylen,
                 const void *value, size_t size) {
    // Don't insert if already in the cache.
    if (hashmap_find(cache->map, key, keylen) != NULL)
        return 0;

    // If the size is too large, we can't cache it and we'll have to take the
    // hit every time.
    if (size > cache->max_size)
        return -1;

    // Create a new block to store the data.
    block_t *block = get_block(key, keylen, value, size);

    // Update the current cache size.
    // Evict blocks until the new block fits.
    cache->size += block->size;

    node_t *head = cache->lru_list->head;
    node_t *n = head;
    while (n != NULL && cache->size > cache->max_size) {
        n = n->prev;
        block_t *b = (block_t *)n->value;
        cache_delete(cache, b);
    }

    // Add the new block.
    hashmap_insert(cache->map, block->key, block->keylen, block);
    list_insert(cache->lru_list, block);

    return 0;
}

/**
 * Lookup an entry in the cache and return a pointer to the block if found.
 *
 * This increments the refcount, meaning that you must call cache_release on the
 * block when finished using it.
 *
 * @param  cache   Pointer to the cache to query.
 * @param  key     Bytestring hashed by the hash table.
 * @param  keylen  Number of bytes to hash.
 * @param  mut     Mutex to lock shared resources when multithreading.
 *
 * @return Pointer to the block if it exists in the cache. NULL otherwise.
 */
block_t *cache_find(cache_t *cache, const void *key, size_t keylen) {
    block_t *block = hashmap_find(cache->map, key, keylen);
    if (block == NULL) {
        return NULL;
    }

    node_t *node = list_find(cache->lru_list, block);
    list_move_to_head(cache->lru_list, node);
    return block;
}
