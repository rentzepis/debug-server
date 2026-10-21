/*
 * Software cache using double linked list with new lines pushed onto head
 */

#include "cache.h"
#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

// Free
void free_cache(cache_t *cache) {
    line_t *line = cache->head;
    line_t *next;

    while (line != NULL) {
        next = line->next;
        free(line);
        line = next;
    }
}

// Check if the cache has reached max size
bool check_cache(cache_t *cache) {
    return ((MAX_CACHE_SIZE - cache->size) < MAX_OBJECT_SIZE);
}

// Stores a new line not in the cache already
void store_line(cache_t *cache, const char *tag, char *obj, unsigned int size) {
    line_t *line = malloc(sizeof(line_t));

    line->tag = tag;
    line->obj = obj;
    line->size = size;
    line->lock = 1;

    line->next = cache->head;
    cache->head->prev = line;

    cache->head = line;
    cache->size += size;

    // If the cache is empty
    if (cache->tail == NULL)
        cache->tail = line;
}

// Gets the web object from the cache and returns NULL on failure
// Adds to the lock if found as the object is being used again
char *get_obj(cache_t *cache, const char *tag) {

    for (line_t *i = cache->head; i != NULL; i = i->next)
        if (!strcmp(i->tag, tag)) {
            i->lock += 1;
            return i->obj;
        }

    return NULL;
}

// Removes an obj instance but does NOT evict for safety
void rem_line(cache_t *cache, const char *tag) {
    for (line_t *i = cache->head; i != NULL; i = i->next)
        if (!strcmp(i->tag, tag)) {
            i->lock -= 1;
            return;
        }
}

// Evicts an unused line in FIFO order
// Exits with failure when empty or nothing is removed as that shouldn't happen
// with this being inside of a loop in proxy.c
void evict_line(cache_t *cache) {
    // Special case for empty cache throws error
    if (cache->tail == NULL) {
        sio_printf("AMBASSING\n");
        exit(1);
    }
    // Special case for 1 line that shouldn't happen but why not
    if (cache->tail->prev == NULL) {
        if (cache->tail->lock < 1) {
            cache->size -= cache->tail->size;
            free(cache->tail);
            cache->head = NULL;
            cache->tail = NULL;
        } else
            sio_printf("AMBATUKAOJDHGfsdjfbilwehfoweo\n");
        exit(1);
        return;
    }

    line_t *next = cache->tail;
    line_t *curr = cache->tail->prev;
    line_t *prev;

    while (curr != NULL) {
        prev = curr->prev;

        if (curr->lock < 1) {
            if (prev != NULL)
                prev->next = next;

            next->prev = prev;
            cache->size -= curr->size;
            free(curr);
            return;
        }

        next = curr;
        curr = prev;
    }

    // No line found so throwing error
    sio_printf("DREAMYBULLSNUTS\n");
    exit(1);
}
