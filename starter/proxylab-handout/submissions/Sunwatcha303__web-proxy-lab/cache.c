#include "cache.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cache_t *cache = NULL;
static int next_id = 0;
pthread_mutex_t next_id_lock = PTHREAD_MUTEX_INITIALIZER;

static void remove_block(block_t *block)
{
    if (block->prev)
    {
        block->prev->next = block->next;
    }
    else
    {
        cache->head = block->next;
    }

    if (block->next)
    {
        block->next->prev = block->prev;
    }
    else
    {
        cache->tail = block->prev;
    }

    cache->size -= block->response_size;
}

static void free_block(block_t *block)
{
    if (block)
    {
        free(block->key);
        free(block->response);
        free(block);
    }
}

static block_t *create_block(const char *key, const unsigned char *response, size_t response_size)
{
    block_t *block = calloc(1, sizeof(block_t));
    if (!block)
        return NULL;

    block->key = strdup(key);
    if (!block->key)
        goto error;

    block->response = Malloc(response_size);
    if (!block->response)
        goto error;

    memcpy(block->response, response, response_size);
    block->response_size = response_size;

    pthread_mutex_lock(&next_id_lock);
    block->last_access = ++next_id;
    pthread_mutex_unlock(&next_id_lock);

    return block;

error:
    if (block)
    {
        free(block->key);
        free(block->response);
        free(block);
    }
    return NULL;
}

void init_cache(void)
{
    if (cache == NULL)
    {
        pthread_mutex_lock(&next_id_lock);
        if (cache == NULL)
        {
            cache_t *new_cache = calloc(1, sizeof(cache_t));
            if (!new_cache)
            {
                pthread_mutex_unlock(&next_id_lock);
                fprintf(stderr, "Failed to allocate cache\n");
                exit(1);
            }
            pthread_mutex_init(&new_cache->lock, NULL);
            cache = new_cache;
        }
        pthread_mutex_unlock(&next_id_lock);
    }
}

block_t *find_block(const char *key)
{
    if (!key || !cache)
        return NULL;

    block_t *block = NULL;
    pthread_mutex_lock(&cache->lock);

    for (block_t *cur = cache->head; cur != NULL; cur = cur->next)
    {
        if (strcmp(cur->key, key) == 0)
        {
            pthread_mutex_lock(&next_id_lock);
            cur->last_access = ++next_id;
            pthread_mutex_unlock(&next_id_lock);

            block = cur;
            break;
        }
    }

    pthread_mutex_unlock(&cache->lock);
    return block;
}

int insert_block(const char *key, const unsigned char *response, size_t response_size)
{
    if (!key || !response || !cache)
        return 0;
    if (response_size > MAX_CACHE_SIZE)
        return 0;

    pthread_mutex_lock(&cache->lock);
    block_t *existing = NULL;
    for (block_t *cur = cache->head; cur != NULL; cur = cur->next)
    {
        if (strcmp(cur->key, key) == 0)
        {
            existing = cur;
            break;
        }
    }

    if (existing)
    {
        pthread_mutex_unlock(&cache->lock);
        return 0;
    }

    block_t *new_block = create_block(key, response, response_size);
    if (!new_block)
    {
        pthread_mutex_unlock(&cache->lock);
        return 0;
    }

    while (cache->size + response_size > MAX_CACHE_SIZE && cache->head)
    {
        block_t *lru = cache->head;
        time_t oldest = lru->last_access;

        for (block_t *curr = cache->head->next; curr != NULL; curr = curr->next)
        {
            if (curr->last_access < oldest)
            {
                oldest = curr->last_access;
                lru = curr;
            }
        }
        remove_block(lru);
        free_block(lru);
    }

    if (cache->tail)
    {
        cache->tail->next = new_block;
        new_block->prev = cache->tail;
        cache->tail = new_block;
    }
    else
    {
        cache->head = cache->tail = new_block;
    }

    cache->size += response_size;
    pthread_mutex_unlock(&cache->lock);
    return 1;
}

void close_cache(void)
{
    if (!cache)
        return;

    pthread_mutex_lock(&cache->lock);
    pthread_mutex_lock(&next_id_lock);

    block_t *curr = cache->head;
    while (curr)
    {
        block_t *next = curr->next;
        free_block(curr);
        curr = next;
    }

    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
    cache = NULL;
    next_id = 0;

    pthread_mutex_unlock(&next_id_lock);
}

void print_cache_stats(void)
{
    if (!cache)
        return;

    pthread_mutex_lock(&cache->lock);
    printf("Total size: %zu bytes\n", cache->size);
    block_t *cur = cache->head;
    while (cur)
    {
        printf("key: %s\n time: %d\n", cur->key, cur->last_access);
        cur = cur->next;
    }

    pthread_mutex_unlock(&cache->lock);
}