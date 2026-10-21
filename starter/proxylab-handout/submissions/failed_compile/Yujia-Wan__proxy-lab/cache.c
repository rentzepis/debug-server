/**
 * @file cache.c
 * @brief A simple cache of recently accessed web content
 *
 * This program implements a cache to the proxy that keeps recently used web
 * objects in memory. It stores the URL of a GET request as a key, and the
 * received corresponding web object from the server limited by maximum size.
 *
 * @author Yujia Wang <yujiawan@andrew.cmu.edu>
 */

#include "cache.h"

cache_t *cache;
pthread_mutex_t mutex;

void init_cache() {
    cache = (cache_t *)malloc(sizeof(cache_t));
    if (cache == NULL) {
        sio_printf("Malloc for cache failed\n");
        return;
    }

    cache->head = NULL;
    cache->tail = NULL;
    cache->size = 0;

    // initialize mutex
    pthread_mutex_init(&mutex, NULL);
    return;
}

void free_cache() {
    if (cache->head != NULL) {
        cache_block_t *curr = cache->head;
        cache_block_t *next;
        while (curr != NULL) {
            next = curr->next;
            free_block(curr);
            curr = next;
        }
    }
    free(cache);
}

cache_block_t *alloc_block(const char *uri, char obj[], ssize_t obj_size) {
    cache_block_t *block = (cache_block_t *)malloc(sizeof(cache_block_t));
    if (block == NULL) {
        sio_printf("Malloc for cache block failed\n");
        return NULL;
    }

    block->url = (char *)malloc(strlen(uri) + 1);
    if (block->url == NULL) {
        sio_printf("Malloc for block url failed\n");
        return NULL;
    }
    strcpy(block->url, uri);

    block->object = (char *)malloc(obj_size);
    if (block->object == NULL) {
        sio_printf("Malloc for block object failed\n");
        return NULL;
    }
    memcpy(block->object, obj, obj_size);

    block->object_size = obj_size;
    block->reference_count = 0;
    return block;
}

void free_block(cache_block_t *block) {
    // When the reference count drops to 0, the object can safely be freed
    while (1) {
        if (block->reference_count == 0) {
            free(block->url);
            free(block->object);
            free(block);
            break;
        }
    }
    return;
}

void insert_head(cache_block_t *block) {
    if (cache->head == NULL) { // linked list is null
        cache->head = block;
        cache->tail = block;
        block->prev = NULL;
        block->next = NULL;
    } else {
        // insert to the head of the list, most recently used block
        if (block == cache->head) {
            return;
        }

        block->next = cache->head;
        cache->head->prev = block;
        block->prev = NULL;
        cache->head = block;

        // only one block in the cache
        if (cache->head->next == NULL) {
            cache->tail->prev = block;
        }
    }

    block->reference_count++;
    return;
}

void remove_tail() {
    if (cache->tail == NULL) {
        return;
    }

    // do eviction, remove the tail of the list
    cache_block_t *old_tail = cache->tail;
    if (cache->tail->prev == NULL) {
        // only one block in cache
        cache->head = NULL;
        cache->tail = NULL;
    } else {
        cache->tail = old_tail->prev;
        cache->tail->next = NULL;
        old_tail->prev = NULL;
    }

    cache->size -= old_tail->object_size;

    old_tail->reference_count--;

    free_block(old_tail);
    return;
}

ssize_t read_cache(const char *uri, int fd) {
    pthread_mutex_lock(&mutex);
    cache_block_t *block = cache->head;
    while (block != NULL) {
        if (!strncasecmp(uri, block->url, strlen(uri))) {
            if (block != cache->head) {
                // move the object to the head of the list
                block->prev->next = block->next;
                if (block == cache->tail) {
                    // update cache tail
                    cache->tail = block->prev;
                } else {
                    block->next->prev = block->prev;
                }
                block->prev = NULL;
                block->next = NULL;
                insert_head(block);
            } else {
                // if head matches, simply add reference count
                block->reference_count++;
            }

            // release lock before transmitting the object to the client
            pthread_mutex_unlock(&mutex);

            // forward the cached web object to the client
            rio_writen(fd, block->object, block->object_size);

            // decrement reference count when it is done transmitting the object
            // to a client
            pthread_mutex_lock(&mutex);
            block->reference_count--;
            pthread_mutex_unlock(&mutex);

            return block->object_size;
        }
        block = block->next;
    }

    // URL not found
    pthread_mutex_unlock(&mutex);
    return -1;
}

void write_cache(const char *uri, char object[], ssize_t object_size) {
    pthread_mutex_lock(&mutex);

    // check uniqueness, if the URL is already in cache, return
    cache_block_t *block = cache->head;
    while (block != NULL) {
        if (!strncasecmp(uri, block->url, strlen(uri))) {
            pthread_mutex_unlock(&mutex);
            return;
        }
        block = block->next;
    }

    while (cache->size + object_size > MAX_CACHE_SIZE) {
        // eviction
        remove_tail();
    }

    // store the web object with its URL in a new cache block and insert to the
    // head of the list
    block = alloc_block(uri, object, object_size);
    insert_head(block);
    cache->size += object_size;

    pthread_mutex_unlock(&mutex);
    return;
}

void print_cache() {
    cache_block_t *block = cache->head;
    while (block != NULL) {
        sio_printf("block:\n");
        sio_printf("  address    : %p\n", block);
        sio_printf("  url        : %s\n", block->url);
        sio_printf("  url length : %zu\n", strlen(block->url));
        sio_printf("  object size: %zu\n", block->object_size);
        if (block->next != NULL) {
            sio_printf("  next block : %p\n", block->next);
        } else {
            sio_printf("  next block : NULL:(\n");
        }
        if (block->prev != NULL) {
            sio_printf("  prev block : %p\n", block->prev);
        } else {
            sio_printf("  prev block : NULL:(\n");
        }
        block = block->next;
    }
}
