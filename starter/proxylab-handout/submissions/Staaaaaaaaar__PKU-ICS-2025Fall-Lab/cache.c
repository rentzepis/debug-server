#include "cache.h"

static Cache cache;
static int read_cnt;
static sem_t mutex, w;

void cache_init() {
    cache.num = 0;
    read_cnt = 0;
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
    for (int i = 0; i < MAX_CACHE; i++) {
        cache.data[i].isEmpty = 1;
        cache.data[i].lru_cnt = 0;
    }
}

static void reader_lock() {
    P(&mutex);
    read_cnt++;
    if (read_cnt == 1) {
        P(&w);
    }
    V(&mutex);
}

static void reader_unlock() {
    P(&mutex);
    read_cnt--;
    if (read_cnt == 0) {
        V(&w);
    }
    V(&mutex);
}

static void writer_lock() {
    P(&w);
}

static void writer_unlock() {
    V(&w);
}

int cache_find(char *url, char *buf, int *size) {
    int found = 0;
    reader_lock();
    for (int i = 0; i < MAX_CACHE; i++) {
        if (!cache.data[i].isEmpty && strcmp(cache.data[i].uri, url) == 0) {
            memcpy(buf, cache.data[i].obj, cache.data[i].size);
            *size = cache.data[i].size;
            found = 1;
            break;
        }
    }
    reader_unlock();
    
    if (found) {
        // Update LRU - this is a write operation on metadata
        writer_lock();
        // Re-find the index because state might have changed (though unlikely to be evicted that fast)
        // But to be safe and simple, we just iterate again.
        for (int i = 0; i < MAX_CACHE; i++) {
             if (!cache.data[i].isEmpty && strcmp(cache.data[i].uri, url) == 0) {
                 cache.data[i].lru_cnt = 0;
                 for(int j = 0; j < MAX_CACHE; j++) {
                    if(j != i && !cache.data[j].isEmpty) {
                        cache.data[j].lru_cnt++;
                    }
                 }
                 break;
             }
        }
        writer_unlock();
        return 1;
    }
    
    return 0;
}

void cache_add(char *url, char *buf, int size) {
    if (size > MAX_OBJECT_SIZE) return;

    writer_lock();

    int victim = -1;
    int max_lru = -1;

    if (cache.num < MAX_CACHE) {
        for (int i = 0; i < MAX_CACHE; i++) {
            if (cache.data[i].isEmpty) {
                victim = i;
                break;
            }
        }
    }

    if (victim == -1) {
        for (int i = 0; i < MAX_CACHE; i++) {
            if (!cache.data[i].isEmpty && cache.data[i].lru_cnt > max_lru) {
                max_lru = cache.data[i].lru_cnt;
                victim = i;
            }
        }
    }

    if (victim != -1) {
        if (cache.data[victim].isEmpty) {
            cache.num++; 
        }

        strcpy(cache.data[victim].uri, url);
        memcpy(cache.data[victim].obj, buf, size);
        cache.data[victim].size = size;
        cache.data[victim].isEmpty = 0;
        cache.data[victim].lru_cnt = 0;
        
        for(int j = 0; j < MAX_CACHE; j++) {
            if(j != victim && !cache.data[j].isEmpty) {
                cache.data[j].lru_cnt++;
            }
        }
    }
    
    writer_unlock();
}
