#include "csapp.h"
#include "cache.h"

cache_entry *head;
cache_entry *tail;
int cache_size;

pthread_mutex_t cache_mutex;

void cache_init()
{
    head = NULL;
    tail = NULL;
    cache_size = 0;

    pthread_mutex_init(&cache_mutex, NULL);
}

cache_entry *cache_find(char *key)
{
    pthread_mutex_lock(&cache_mutex);

    cache_entry *curr = head;
    while (curr != NULL) {
        if (strcmp(curr->key, key) == 0) {
            // Move to front
            if (curr != head) {
                curr->prev->next = curr->next;
                if (curr != tail) {
                    curr->next->prev = curr->prev;
                } else {
                    tail = curr->prev;
                }
                curr->prev = NULL;
                curr->next = head;
                head->prev = curr;
                head = curr;
            }

            printf("Cache hit!\n");
            return curr;
        }
        curr = curr->next;
    }

    pthread_mutex_unlock(&cache_mutex);

    return NULL;
}

void cache_insert(char *key, char *value, int size)
{
    pthread_mutex_lock(&cache_mutex);

    if (cache_size + size > MAX_CACHE_SIZE) {
        cache_evict(size);
    }

    cache_entry *entry = Malloc(sizeof(cache_entry));
    entry->key = strdup(key);
    entry->value = strdup(value);
    entry->size = size;

    entry->prev = NULL;
    entry->next = head;
    if (head != NULL) {
        head->prev = entry;
    } else {
        tail = entry;
    }
    head = entry;

    cache_size += size;

    pthread_mutex_unlock(&cache_mutex);

    printf("Cache miss!\n");
}

void cache_evict(int size)
{
    printf("Evicting %d bytes\n", size);
    
    while (cache_size + size > MAX_CACHE_SIZE && tail != NULL) {
        cache_entry *prev = tail->prev;
        cache_size -= tail->size;
        Free(tail->key);
        Free(tail->value);
        Free(tail);
        tail = prev;
        if (tail != NULL) {
            tail->next = NULL;
        } else {
            head = NULL;
        }
    }

    printf("Cache size: %d\n", cache_size);
}

void cache_free()
{
    cache_entry *curr = head;
    while (curr != NULL) {
        cache_entry *next = curr->next;
        Free(curr->key);
        Free(curr->value);
        Free(curr);
        curr = next;
    }

    pthread_mutex_destroy(&cache_mutex);
}