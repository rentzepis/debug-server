#include "cache.h"
#include <string.h>
#include "csapp.h"
/* Global variables */

static void addNode();
unsigned long hash_func(char *str, size_t str_size);
static cache_node_t head_node;
static cache_node_t tail_node;
void add_node(cache_t *cache, cache_node_t *new_node);
void remove_node(cache_t *cache);
void init_cache_node(unsigned long hash, cache_node_t *node);

int init_cache(size_t cache_size, cache_t *cache)
{
    head_node.next = &tail_node;
    tail_node.prev = &head_node;
    cache->head = &head_node;
    cache->tail = &tail_node;
    cache->length = 0;
    cache->max_length = cache_size;
}

/**
 * insert_cache - insert buf to the cache
 */
void insert_cache(cache_t *cache, char *buf, size_t buf_size)
{
    memcpy(cache->head->next->buf, buf, buf_size);
    cache->head->next->buf_size = buf_size;
}

/**
 * lookup_cache - Check whether a buffer content is cached
 * return value: cache_node_t pointer if found, NULL if not found
 */
cache_node_t *lookup_cache(cache_t *cache, char *url)
{

    // Looking for the cache
    unsigned long cur_buf_hash = hash_func(url, strlen(url));

    for (cache_node_t *ptr = cache->head->next; ptr; ptr = ptr->next)
    {
        // 如果找到了，将这个node提到最前面
        if (ptr->hash == cur_buf_hash)
        {
            prior_node(cache, ptr);
            return ptr;
        }
    }

    cache_node_t *new_node = malloc(sizeof(cache_node_t));
    init_cache_node(cur_buf_hash, new_node);
    /* insert node with hash but without buffered webpage into cache*/
    if ((cache->length) < (cache->max_length))
    {
        add_node(cache, new_node);
    }
    else
    {
        remove_node(cache);
        add_node(cache, new_node);
    }

    return NULL;
}

/**
 * hash_func - Hash function to be applied to the request. Borrowed from:
 *             https://stackoverflow.com/a/7666577/9057530
 */
unsigned long hash_func(char *str, size_t str_size)
{
    unsigned long res = 5381;
    int c;

    for (size_t i = 0; i < str_size; i++)
    {

        c = *(str++);
        res = ((res << 5) + res) + c; /* hash * 33 + c */
    }

    return res;
}

/**
 * init_cache_node: 初始化cache node
 */
void init_cache_node(unsigned long hash, cache_node_t *node)
{
    node->prev = NULL;
    node->next = NULL;
    node->hash = hash;
    node->buf_size = 0;
}

/**
 * add_node: helper function to remove a node from cache
 */
void add_node(cache_t *cache, cache_node_t *new_node)
{
    new_node->next = cache->head->next;
    new_node->prev = cache->head;
    new_node->next->prev = new_node;
    cache->head->next = new_node;
    cache->length++;
    return;
}

/**
 * add_node: helper function to remove a node from cache
 */
void prior_node(cache_t *cache, cache_node_t *new_node)
{
    if (cache->head->next = new_node)
        return;
    new_node->next = cache->head->next;
    new_node->prev = cache->head;
    cache->head->next = new_node;
    new_node->next->prev = new_node;
    return;
}

/**
 * remove_node: helper function to add a node from cache
 */
void remove_node(cache_t *cache)
{
    if (cache->length == 0)
        return;
    cache_node_t *removed_node = cache->tail->prev;
    removed_node->prev->next = cache->tail;
    cache->tail->prev = removed_node->prev;
    free(removed_node);
    cache->length--;
    return;
}