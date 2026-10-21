#include "csapp.h"
#include "cache.h"

void cache_init(Cache *cache) {
    cache->num = 0;
    for (int i = 0; i < MAX_CACHE; i++) {
        cache->data[i].LRU = 0;
        cache->data[i].isEmpty = 1;
        cache->data[i].read_cnt = 0;
        Sem_init(&(cache->data[i].write), 0, 1);
        Sem_init(&(cache->data[i].read), 0, 1);
    } 
}

/* Read operation */
int in_cache(Cache *cache, char *url) {
    int i;
    for (i = 0; i < MAX_CACHE; i++) {
        P(&cache->data[i].read);          // gain read lock
        cache->data[i].read_cnt++;
        if (cache->data[i].read_cnt == 1)
            P(&cache->data[i].write);     // gain write lock
        V(&cache->data[i].read);          // release read lock

        // find the target
        if ((cache->data[i].isEmpty == 0) && (strcmp(url, cache->data[i].uri) == 0))
            break;

        P(&cache->data[i].read);
        cache->data[i].read_cnt--;
        if (cache->data[i].read_cnt == 0)
            V(&cache->data[i].write);
        V(&cache->data[i].read);
    }

    if (i >= MAX_CACHE) return -1;
    return i;
}

/* Write operation */
void write_cache(Cache *cache, char *uri, char *buf) {
    int index;
    if ((index = get_index(cache)) == -1) {
        printf("Error: Unable to find a cache slot\n");
        return;
    }
    P(&cache->data[index].write);
    strcpy(cache->data[index].obj, buf);
    strcpy(cache->data[index].uri, uri);
    cache->data[index].isEmpty = 0;
    cache->data[index].LRU = 0;
    V(&cache->data[index].write);
    update_LRU(cache, index);
}

/* Write operation */
int get_index(Cache *cache) {
    int max_LRU = 0;
    int index = -1;

    for (int i = 0; i < MAX_CACHE; i++) {
        P(&cache->data[i].write);

    /* Situation1: cache has empty position */
        if (cache->data[i].isEmpty == 1) {
            index = i;
            V(&cache->data[i].write);
            break;
        }

    /* Situation2: cache has no empty position, LRU-evict */
        if (cache->data[i].LRU > max_LRU) {
            max_LRU = cache->data[i].LRU;
            index = i;
        }
        V(&cache->data[i].write);
    }

    return index;
}

/* Write operation */
void update_LRU(Cache *cache, int index) {
    for (int i = 0; i < MAX_CACHE; i++)
    {
        if (cache->data[i].isEmpty == 0 && i != index)
        {
            P(&cache->data[i].write);
            cache->data[i].LRU++;
            V(&cache->data[i].write);
        }
    }
}