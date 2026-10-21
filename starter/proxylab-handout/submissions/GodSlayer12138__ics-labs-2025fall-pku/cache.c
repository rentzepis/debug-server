#include "cache.h"

static Cache_block cache[CACHE_OBJS_COUNT];
static int global_time = 0;
static int readcnt;
static sem_t mutex, w;

void cache_init() {
    global_time = 0;
    readcnt = 0;
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
    for (int i = 0; i < CACHE_OBJS_COUNT; i++) {
        cache[i].is_empty = 1;
        cache[i].lru_time = 0;
        cache[i].cache_size = 0;
    }
}

int read_cache(char *url, int fd) {
    int i;
    char *data = NULL;
    int size = 0;

    /* 读者优先的读者-写者模型 */
    P(&mutex);
    readcnt++;
    if (readcnt == 1) P(&w); // 第一个读者加锁
    V(&mutex);

    for (i = 0; i < CACHE_OBJS_COUNT; i++) {
        if (!cache[i].is_empty && strcmp(url, cache[i].cache_url) == 0) {
            data = (char *)Malloc(cache[i].cache_size);
            memcpy(data, cache[i].cache_data, cache[i].cache_size);
            size = cache[i].cache_size;
            break;
        }
    }

    P(&mutex);
    readcnt--; // 最后一个读者解锁
    if (readcnt == 0) V(&w);
    V(&mutex);

    if (data != NULL) {
        rio_writen(fd, data, size);
        Free(data);

        P(&w);
        // 重新找到该块以更新 LRU 时间戳
        for (i = 0; i < CACHE_OBJS_COUNT; i++) {
            if (!cache[i].is_empty && strcmp(url, cache[i].cache_url) == 0) {
                cache[i].lru_time = ++global_time;
                break;
            }
        }
        V(&w);
        return 1;
    }
    return 0;
}

void write_cache(char *url, char *data, int len) {
    int i;
    int min_lru = 0x7fffffff;
    int min_idx = -1;

    P(&w);
    
    // 检查是否已在缓存中
    for (i = 0; i < CACHE_OBJS_COUNT; i++) {
        if (!cache[i].is_empty && strcmp(url, cache[i].cache_url) == 0) {
            cache[i].lru_time = ++global_time;
            V(&w);
            return;
        }
    }

    // 寻找空闲块或 LRU（最近最少使用）块
    for (i = 0; i < CACHE_OBJS_COUNT; i++) {
        if (cache[i].is_empty) {
            min_idx = i;
            break;
        }
        if (cache[i].lru_time < min_lru) {
            min_lru = cache[i].lru_time;
            min_idx = i;
        }
    }

    if (min_idx != -1) {
        cache[min_idx].is_empty = 0;
        strcpy(cache[min_idx].cache_url, url);
        memcpy(cache[min_idx].cache_data, data, len);
        cache[min_idx].cache_size = len;
        cache[min_idx].lru_time = ++global_time;
    }

    V(&w);
}
