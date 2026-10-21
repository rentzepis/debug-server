#include "cache.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Initialize cache */
void cache_init(cache_t *cache) {
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_size = 0;
    Sem_init(&cache->mutex, 0, 1);
    Sem_init(&cache->w, 0, 1);
    cache->readcnt = 0;
    Sem_init(&cache->readcnt_mutex, 0, 1);
}

/* Deinitialize cache and free all memory */
void cache_deinit(cache_t *cache) {
    cache_node_t *node = cache->head;
    while (node) {
        cache_node_t *next = node->next;
        Free(node->url);
        Free(node->content);
        Free(node);
        node = next;
    }
}

/* Move node to front (LRU) */
static void cache_move_to_front(cache_t *cache, cache_node_t *node) {
    if (node == cache->head) {
        return;  // Already at front
    }

    // Remove from current position
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    if (node == cache->tail) {
        cache->tail = node->prev;
    }

    // Move to front
    node->prev = NULL;
    node->next = cache->head;
    if (cache->head) {
        cache->head->prev = node;
    }
    cache->head = node;
    if (!cache->tail) {
        cache->tail = node;
    }
}

/* Find a node in cache by URL and move to front (LRU) */
cache_node_t *cache_find(cache_t *cache, const char *url) {
    P(&cache->readcnt_mutex);
    cache->readcnt++;
    if (cache->readcnt == 1) {
        P(&cache->w);  // First reader locks writers
    }
    V(&cache->readcnt_mutex);

    cache_node_t *node = cache->head;
    cache_node_t *found = NULL;
    
    while (node) {
        if (strcmp(node->url, url) == 0) {
            found = node;
            break;
        }
        node = node->next;
    }

    /* Move found node to front for LRU */
    if (found) {
        cache_move_to_front(cache, found);
    }

    P(&cache->readcnt_mutex);
    cache->readcnt--;
    if (cache->readcnt == 0) {
        V(&cache->w);  // Last reader unlocks writers
    }
    V(&cache->readcnt_mutex);

    return found;
}

/* Evict least recently used node */
void cache_evict_lru(cache_t *cache) {
    if (!cache->tail) {
        return;
    }

    cache_node_t *node = cache->tail;
    
    // Remove from list
    if (node->prev) {
        node->prev->next = NULL;
    } else {
        cache->head = NULL;
    }
    cache->tail = node->prev;

    // Update size and free memory
    cache->total_size -= (strlen(node->url) + node->content_size + sizeof(cache_node_t));
    Free(node->url);
    Free(node->content);
    Free(node);
}

/* Insert into cache */
void cache_insert(cache_t *cache, const char *url, const char *content, size_t content_size) {
    // Check if object is too large
    if (content_size > MAX_OBJECT_SIZE) {
        return;
    }

    size_t url_len = strlen(url);
    size_t node_size = url_len + content_size + sizeof(cache_node_t);

    P(&cache->w);  // Writer lock

    // Check if already exists
    cache_node_t *existing = cache->head;
    while (existing) {
        if (strcmp(existing->url, url) == 0) {
            // Update existing node
            if (existing->content_size != content_size) {
                // Size changed, need to reallocate
                cache->total_size -= (url_len + existing->content_size);
                Free(existing->content);
                existing->content = Malloc(content_size);
                memcpy(existing->content, content, content_size);
                existing->content_size = content_size;
                cache->total_size += node_size;
            } else {
                // Same size, just update content
                memcpy(existing->content, content, content_size);
            }
            cache_move_to_front(cache, existing);
            V(&cache->w);
            return;
        }
        existing = existing->next;
    }

    // Evict nodes until we have enough space
    while (cache->total_size + node_size > MAX_CACHE_SIZE && cache->tail) {
        cache_evict_lru(cache);
    }

    // Create new node
    cache_node_t *node = Malloc(sizeof(cache_node_t));
    node->url = Malloc(url_len + 1);
    strcpy(node->url, url);
    node->content = Malloc(content_size);
    memcpy(node->content, content, content_size);
    node->content_size = content_size;
    node->prev = NULL;
    node->next = cache->head;

    // Insert at front
    if (cache->head) {
        cache->head->prev = node;
    }
    cache->head = node;
    if (!cache->tail) {
        cache->tail = node;
    }

    cache->total_size += node_size;
    V(&cache->w);
}

