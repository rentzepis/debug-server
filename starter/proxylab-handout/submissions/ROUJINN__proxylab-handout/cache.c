/* cache.c - a cache in proxy that caches content received from server.
 * uses LRU eviction policy and has 10 lines.
 */
#include "csapp.h"
#include "cache.h"

static int readcnt;
static int cache_time = 0;
static sem_t mutex,w;

line* cache[10];

/* find an empty line in cache. If fails, returns -1*/
static int get_empty_line() {
    for (int i=0; i<10; i++) {
        if (cache[i]->url == 0) {
            return i;
        }
    }
    return -1;
}

/* uses LRU to find a eviction line in cache */
static int get_evict_line() {
    int min_time_line = 0;
    int min_time = cache[min_time_line] -> time;
    for (int i=0; i<10; i++) {
        if (cache[i] -> time < min_time) {
            min_time_line = i;
            min_time = cache[i] -> time;
        }
    }
    return min_time_line;    
}

/* initialize the cache */
void init_cache() {
    for (int i=0; i<10; i++) {
        cache[i] = calloc(1,sizeof(line));
    }
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
}

/* read the cache to find a match line. If succeed, write directly to the 
 * client. Otherwise return -1 
 */
int read_cache(char *url, int fd) {
    
    P(&mutex);
    readcnt++;
    if (readcnt == 1) {
        P(&w);
    }
    V(&mutex);

    int target_line = -1;
    for (int i=0; i<10; i++) {
        if (!strcmp((cache[i]->url),url)) {
            target_line = i;
            Rio_writen(fd, cache[i]->payload, cache[i]->size);
            break;
        }
    }

    P(&mutex);
    readcnt--;
    if (readcnt == 0) {
        V(&w);
    }
    V(&mutex);

    return target_line;   
}

/* write object to cache */
void write_cache(char *object, int size, char *url) {
    P(&w);
    cache_time ++;

    int target_line;
    target_line = get_empty_line();
    if (target_line != -1) {
        memcpy(cache[target_line]->payload, object, size);
        strcpy(cache[target_line]->url, url);
        cache[target_line]->time = cache_time;
        cache[target_line]->size = size;
    }
    else {
        target_line = get_evict_line();
        memcpy(cache[target_line]->payload, object, size);
        strcpy(cache[target_line]->url, url);
        cache[target_line]->time = cache_time;
        cache[target_line]->size = size;
    }
    V(&w);
}

/* free the cache */
void free_cache() {
    for (int i = 0; i<10; i++) {
        free(cache[i]);
    }
}
