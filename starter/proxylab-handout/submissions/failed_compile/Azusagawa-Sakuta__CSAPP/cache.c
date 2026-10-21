#include "cache.h"

extern Cache cache;
extern int timestamp;

void cache_init() {
    cache.readcnt = 0;
    Sem_init(&cache.mutex, 0, 1);
    Sem_init(&cache.w, 0, 1);
    for (int i = 0; i < MAX_CACHE_SIZE/MAX_OBJECT_SIZE; i++) {
        cache.cache_block[i].length = 0;
        cache.cache_block[i].valid = 0;
        cache.cache_block[i].timestamp = 0;
    }
}

int cache_read(char* uri, int fd) {
    int idx = -1;
    for (int i = 0; i < MAX_CACHE_SIZE/MAX_OBJECT_SIZE; i++) {
        if (strcmp(cache.cache_block[i].uri, uri) == 0) {
            idx = i;
            break;
        }
    }
    if (idx == -1) return 0;
    P(&cache.mutex);
    cache.readcnt++;
    if (cache.readcnt == 1) P(&cache.w);
    V(&cache.mutex);

    Rio_writen(fd, cache.cache_block[idx].buf, cache.cache_block[idx].length);

    P(&cache.mutex);
    cache.readcnt--;
    if (cache.readcnt == 0) V(&cache.w);
    V(&cache.mutex);
    return 1;
}

void cache_write(char* msg2send, char* uri, ssize_t len) {
    int idx = -1;
    int now = 0;
    for (int i = 0; i < MAX_CACHE_SIZE/MAX_OBJECT_SIZE; i++) {
        if (cache.cache_block[i].valid == 0) {
            idx = i;
            break;
        }
    }
    if (idx == -1) {
        for (int i = 0; i < MAX_CACHE_SIZE/MAX_OBJECT_SIZE; i++) {
            if (timestamp - cache.cache_block[i].timestamp > now) {
                now = timestamp - cache.cache_block[i].timestamp;
                idx = i;
            }
        }
    }
    
    P(&cache.w);
    for(int i = 0; i < len; i++) {
        cache.cache_block[idx].buf[i] = msg2send[i];
    }
    strcpy(cache.cache_block[idx].uri, uri);
    cache.cache_block[idx].length = len;
    cache.cache_block[idx].timestamp = timestamp;
    timestamp++;
    cache.cache_block[idx].valid = 1;
    V(&cache.w);
}