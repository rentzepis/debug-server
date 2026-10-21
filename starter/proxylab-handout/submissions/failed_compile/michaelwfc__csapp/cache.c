

#include <stdlib.h> // For malloc, free
#include <string.h> // For strcmp, memcpy, strdup
#include "cache.h"
#include <pthread.h>

void evict_from_cache(CacheList *cache);
void move_to_front(CacheList *cache, CacheBlock *block);
void remove_from_list(CacheList *cache, CacheBlock *block);
void insert_at_front(CacheList *cache, CacheBlock *block);

// build cache
CacheList *cache_build()
{
    CacheList *cache = malloc(sizeof(CacheList));
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    return cache;
}

void cache_free(CacheList *cache)
{
    CacheBlock *p = cache->head;
    while (p)
    {
        CacheBlock *next = p->next;
        free(p->url);
        free(p->content);
        free(p);
        p = next;
    }
    free(cache);
}

// Cache Lookup
char *cache_get(CacheList *cache, const char *url, int *object_size)
{
    pthread_rwlock_rdlock(&cache->lock);

    CacheBlock *p = cache->head;
    while (p)
    {
        if (strcmp(p->url, url) == 0)
        {
            // Move this block to the front (most recently used)
            move_to_front(cache, p);
            *object_size = p->size;

            char *copy = malloc(p->size);
            memcpy(copy, p->content, p->size);
            pthread_rwlock_unlock(&cache->lock);
            return copy;
        }
        p = p->next;
    }

    pthread_rwlock_unlock(&cache->lock);
    return NULL;
}

void move_to_front(CacheList *cache, CacheBlock *block)
{
    if (block == cache->head)
        return;

    remove_from_list(cache, block);
    insert_at_front(cache, block);
}

void remove_from_list(CacheList *cache, CacheBlock *block)
{
    // block -> prev -> next  =  block -> next
    // block -> next-> prev   =  block -> prev
    if (block == cache->head)
    {
        cache->head = block->next;
    }
    else
    {
        block->prev->next = block->next;
    }

    if (block == cache->tail)
    {
        cache->tail = block->prev;
    }
    else
    {
        block->next->prev = block->prev;
    }
}

void insert_at_front(CacheList *cache, CacheBlock *block)
{
    block->next = cache->head;
    block->prev = NULL;

    if (cache->head)
    {
        //  when cache->head is not NULL,otherwise occurs The segmentation fault
        cache->head->prev = block;
    }else{
        cache->tail = block;
    }
    cache->head = block;
    
}

// Eviction Policy (Approx LRU)
int cache_put(CacheList *cache, const char *url, const char *buf, int size)
{   
    int total_size = cache->total_size + size;
    if (size > MAX_OBJECT_SIZE || total_size > MAX_CACHE_SIZE)
        return;

    pthread_rwlock_wrlock(&cache->lock);

    // Evict until there’s space
    while (cache->total_size + size > MAX_CACHE_SIZE)
    {
        evict_from_cache(cache);
    }

    CacheBlock *new_block = malloc(sizeof(CacheBlock));
    new_block->url = strdup(url);
    new_block->content = malloc(size);
    memcpy(new_block->content, buf, size);
    new_block->size = size;

    insert_at_front(cache, new_block);
    cache->total_size += size;

    pthread_rwlock_unlock(&cache->lock);
    return 0;
}

void Cache_put(CacheList *cache, const char *url, const char *buf, int size)
{
    int rc;

    if ((rc = cache_put(cache, url, buf, size)) != 0)
    {
        posix_error(rc, "Cache_put error");
        exit(0);
    }
}

void evict_from_cache(CacheList *cache)
{
    if (!cache->tail)
        return;

    CacheBlock *victim = cache->tail;

    remove_from_list(cache, victim);
    cache->total_size -= victim->size;
    free(victim->url);
    free(victim->content);
    free(victim);
}
