#include <stdio.h>
#include "./csapp.h"
#include "./cache.h"

static cache_t cache; // shared object
static int timestamp; // shared object, +1 in add_cache
static int readcnt;   // shared object

// mutex semaphores protect to access to the the shared readcnt
// w semaphores protect access to the critical section that access the shared object
static sem_t mutex, mutex_ts, w;  // mutex protect readcnt, mutex_ts protect timestamp


void init_cache() {
    sem_init(&mutex, 0, 1);
    sem_init(&mutex_ts, 0, 1);
    sem_init(&w, 0, 1);

    cache.current_cache_num = 0;
    timestamp = 0;
    readcnt = 0;

    //init cache block
    for (int i = 0; i < MAX_OBJECT_NUM; i++) {
        cache_block *block = &cache.cache_list[i];

        block->url[0] = '\0'; 
        block->size = 0;
        block->timestamp = 0;
    }
}

/*
 * equal to writer to thread cache
 * if success, update current_cache_num
 *
 * 
 */

void add_cache(char *url, const char *buf, size_t size) {
    P(&w);
    timestamp++;
    printf("I am in add_cache\n"); fflush(stdout);
    // if index = 9, must LDR find the evict index and repalce it with new data
    if (cache.current_cache_num == MAX_OBJECT_NUM - 1) {
        // eviction according timestamp
        cache_block *block;
        int min_time = cache.cache_list[0].timestamp;
        int min_index = 0;
        for (int i = 1; i < MAX_OBJECT_NUM; i++) {
            if (cache.cache_list[i].timestamp < min_time) {
                min_index = i;
            }
        }
        //printf("the min_index in cache is: %d, and the url is: %s\n", min_index, url); fflush(stdout);
        // transfer data
        // put data into cache->cache_list[min_index], 
        // must make sure the length of url < MAXLINE
        block = &cache.cache_list[min_index];

        strncpy(block->url, url, strlen(url));
        block->url[strlen(url)] = '\0';
        memcpy(block->data, buf, size);
        block->size = size;
        block->timestamp = timestamp;
    } else {
        // if index != 9, add data at index current_cache_num
        //transfer data
        cache_block *block = &cache.cache_list[cache.current_cache_num];
        printf("In add_cache current_cache_num is: %d\n", cache.current_cache_num);

        //P(&block.w);
        strncpy(block->url, url, strlen(url));
        block->url[strlen(url)] = '\0';
        memcpy(block->data, buf, size);
        block->size = size;
        block->timestamp = timestamp;
        
        printf("Cache[%d]'s url: %s\n, data: %s\n, size: %ld\n, timestamp: %d\n", 
        cache.current_cache_num, block->url, block->data, block->size, block->timestamp);
        cache.current_cache_num++;
    }
    V(&w);

}

/*
 * If can find url in cache return 1 and transfer data to connfd
 * else return 0
 * don't forget add timestamp
 */
int query_cache(char *url, rio_t client_rio) {
    int ret = 0;
    P(&mutex);  // protect readcnt
    readcnt++;
    if (readcnt == 1)
        P(&w); // protect critical section
    V(&mutex);
    
    // critical section
    int connfd = client_rio.rio_fd;
    printf("I am in query_cache, url: %s\n connfd is: %d\n current_cachce_num is: %d\n", 
    url, connfd, cache.current_cache_num); fflush(stdout);
    for (int i = 0; i < cache.current_cache_num; i++) {
        cache_block *block = &cache.cache_list[i];
        printf("In query cache, url: %s\n block[%d].url: %s\n strlen(url): %ld\n block->size: %ld\n", 
        url, i, block->url, strlen(url), block->size);
        if (strcmp(url, block->url) == 0) {
            ret = 1;

            P(&mutex_ts);
            timestamp++;
            V(&mutex_ts);

            Rio_writen(connfd, block->data, block->size);
            break;
        }
    }
    printf("find url in query_cache: %d\n", ret);

    P(&mutex);
    readcnt--;
    if (readcnt == 0)
        V(&w); 
    V(&mutex);
    return ret;
}