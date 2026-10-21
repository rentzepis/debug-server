#include "csapp.h"
#include "cache.h"

static cache_t cache;
static sem_t mutex, w;
static int readcnt, timestamp;

void init_cache() {
    cache.using_cache_num = 0;
    readcnt = 0;
    timestamp = 0;
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
}

int query_cache(rio_t* rio_p, string url) {
    int i;
    P(&mutex);
    readcnt++;
    if (readcnt == 1) {
        P(&w);
    }
    V(&mutex);

    for (i = 0; i < cache.using_cache_num; i++) {
        if (strcmp(cache.cache_files[i].url, url) == 0) {
            rio_writen(rio_p->rio_fd, cache.cache_files[i].content, cache.cache_files[i].content_size);
            P(&mutex);
            cache.cache_files[i].timestamp = timestamp++;
            V(&mutex);
            break;
        }
    }

    P(&mutex);
    readcnt--;
    if (readcnt == 0) {
        V(&w);
    }
    V(&mutex);

    return i != cache.using_cache_num;
}

int add_cache(string url, char* content, int content_size) {
    P(&w);
    if (cache.using_cache_num < MAX_CAHCE_NUM) {
        strcpy(cache.cache_files[cache.using_cache_num].url, url);
        memcpy(cache.cache_files[cache.using_cache_num].content, content, content_size);
        cache.cache_files[cache.using_cache_num].content_size = content_size;
        P(&mutex);
        cache.cache_files[cache.using_cache_num].timestamp = timestamp++;
        V(&mutex);
        cache.using_cache_num++;
    } else {
        int i, lru_index = 0;
        for (i = 1; i < MAX_CAHCE_NUM; i++) {
            if (cache.cache_files[i].timestamp < cache.cache_files[lru_index].timestamp) {
                lru_index = i;
            }
        }
        strcpy(cache.cache_files[lru_index].url, url);
        memcpy(cache.cache_files[lru_index].content, content, content_size);
        cache.cache_files[lru_index].content_size = content_size;
        P(&mutex);
        cache.cache_files[lru_index].timestamp = timestamp++;
        V(&mutex);
    }
    V(&w);
    return 0;
}