#include "cache.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    cache_obj_t *head; // LRU
    cache_obj_t *tail; // MRU
    size_t size;       // bytes of whole cache
    pthread_mutex_t mutex;
} cache_t;

static cache_t cache;

/*
 * init global cache for storing web objects
 */
void init_cache() {
    cache.head = NULL;
    cache.tail = NULL;
    cache.size = 0;
    pthread_mutex_init(&cache.mutex, NULL);
};

static void remove_cache_obj_from_cache(cache_obj_t *obj) {
    if (!obj) {
        return;
    }

    if (obj->next) {
        obj->next->prev = obj->prev;
    } else {
        cache.tail = obj->prev;
    }

    if (obj->prev) {
        obj->prev->next = obj->next;
    } else {
        cache.head = obj->next;
    }

    obj->next = NULL;
    obj->prev = NULL;
}

/*
 * check if current cache's empty space is enough for needed_size
 * if enough: return
 * else: keep evicting LRU web_objs in cache until
 *       empty space >= needed_size
 */
static void evict_obj_in_cache(size_t needed_size) {
    while (needed_size + cache.size > MAX_CACHE_SIZE) {
        // clean from LRU which is not in use
        cache_obj_t *curr = cache.head;
        while (curr && curr->reference_cnt > 0) {
            curr = curr->next;
        }
        if (curr == NULL) {
            break;
        }

        remove_cache_obj_from_cache(curr);
        cache.size -= curr->size;
        free(curr->key);
        free(curr->web_obj);
        free(curr);
    }
}

/*
 * insert a web obecjt (MRU) to cache's tail
 * internal usage for specific operaion
 */
static void insert_cache_obj_to_tail(cache_obj_t *obj) {
    obj->next = NULL;
    obj->prev = cache.tail;
    if (cache.tail) {
        cache.tail->next = obj;
    }
    cache.tail = obj;
    if (cache.head == NULL) {
        cache.head = obj;
    }
}

/*
 * insert a web obecjt to cache
 * public usage for generally insert a new web_obj to cache
 * also include size validation checking
 */
void insert_cache_obj_to_cache(char *key, size_t size, char *web_obj) {
    if (size > MAX_OBJECT_SIZE) {
        return;
    }

    pthread_mutex_lock(&cache.mutex);

    // D15/D16 avoid duplicate insertion
    cache_obj_t *curr = cache.head;
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            pthread_mutex_unlock(&cache.mutex);
            return;
        }
        curr = curr->next;
    }

    evict_obj_in_cache(size);
    // create cache_obj ==========
    cache_obj_t *obj = malloc(sizeof(cache_obj_t));
    obj->key = strdup(key);
    obj->web_obj = malloc(size);
    memcpy(obj->web_obj, web_obj, size);
    obj->size = size;
    obj->reference_cnt = 0;
    obj->prev = NULL;
    obj->next = NULL;
    // ============================
    insert_cache_obj_to_tail(obj);
    cache.size += size;
    pthread_mutex_unlock(&cache.mutex);
};

/*
 * search if a uri request had been cached by passing it as a key
 * if hit: return the cache_obj
 * else miss: return NULL
 */
cache_obj_t *search_cache_obj(const char *key) {
    pthread_mutex_lock(&cache.mutex);

    cache_obj_t *curr = cache.head;
    // hit
    while (curr) {
        if (strcmp(curr->key, key) == 0) {
            curr->reference_cnt++; // increment when a thread retrieves it from
                                   // cache
            // move that cache_obj to tail: make it LRU
            if (cache.tail != curr) {
                remove_cache_obj_from_cache(curr);
                insert_cache_obj_to_tail(curr);
            };
            pthread_mutex_unlock(&cache.mutex);
            return curr;
        }
        curr = curr->next;
    }

    // miss
    pthread_mutex_unlock(&cache.mutex);
    return NULL;
};

/*
 * free a cache_obj whicin was in use in the cache
 */
void free_cache_obj(cache_obj_t *obj) {
    if (!obj)
        return;
    pthread_mutex_lock(&cache.mutex);
    obj->reference_cnt--;
    pthread_mutex_unlock(&cache.mutex);
};