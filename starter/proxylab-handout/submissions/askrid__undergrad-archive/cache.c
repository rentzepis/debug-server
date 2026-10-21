#include "cache.h"

size_t total_cache_size = 0;

CacheNode *init_cache()
{
    CacheNode *head, *tail;

    head = (CacheNode *)malloc(sizeof(CacheNode));
    tail = (CacheNode *)malloc(sizeof(CacheNode));

    head->url = "";
    head->data = "";
    head->size = 0;
    head->prev = head;
    head->next = tail;

    tail->url = "";
    tail->data = "";
    tail->size = 0;
    tail->prev = head;
    tail->next = tail;

    return head;
}

void cache_obj(char *url, char *data, size_t size, CacheNode *cache)
{
    if (size > MAX_OBJECT_SIZE || size == 0)
        return;

    while (total_cache_size + size > MAX_CACHE_SIZE)
        evict(cache);

    CacheNode *new = (CacheNode *)malloc(sizeof(CacheNode));
    new->url = (char *)malloc(strlen(url) + 1);
    new->data = (char *)malloc(strlen(data) + 1);

    strcpy(new->url, url);
    strcpy(new->data, data);
    new->size = size;
    new->prev = cache;
    new->next = cache->next;

    cache->next->prev = new;
    cache->next = new;

    total_cache_size += size;
}

void evict(CacheNode *cache)
{
    CacheNode *node = cache->next;

    while (node->size != 0)
        node = node->next;

    /* The last node is dummy tail. */
    node = node->prev;

    /* Do nothing if the node is dummy head. */
    if (node->size == 0)
        return;

    free(node->url);
    free(node->data);

    node->prev->next = node->next;
    node->next->prev = node->prev;

    total_cache_size -= node->size;
    free(node);
}

CacheNode *find(char *url, CacheNode *cache)
{
    CacheNode *node = cache->next;

    while (node->size != 0 && strcmp(node->url, url))
        node = node->next;

    /* Not found. */
    if (node->size == 0 || strcmp(node->url, url))
        return NULL;

    return node;
}

void to_front(char *url, CacheNode *cache)
{
    CacheNode *node = find(url, cache);

    if (node == NULL)
        return;

    node->prev->next = node->next;
    node->next->prev = node->prev;

    node->prev = cache;
    node->next = cache->next;

    cache->next->prev = node;
    cache->next = node;
}

void destruct_cache(CacheNode *cache)
{
    while (total_cache_size > 0)
        evict(cache);

    /* Free dummy tail and head. */
    free(cache->next);
    free(cache);
}
