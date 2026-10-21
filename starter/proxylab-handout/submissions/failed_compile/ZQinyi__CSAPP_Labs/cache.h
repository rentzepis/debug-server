#ifndef __CACHE_H__
#define __CACHE_H__
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE 10

typedef struct {
    char obj[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int LRU;
    int isEmpty;
    int read_cnt;
    sem_t read;  // protect read_cnt
    sem_t write;  // write lock
} Block;

typedef struct { 
    Block data[MAX_CACHE];  // the whole cache
    int num;                // number of current items in the cache
} Cache;

void cache_init(Cache *cache);
int in_cache(Cache *cache, char *url);
void write_cache(Cache *cache, char *uri, char *buf);
int get_index(Cache *cache);
void update_LRU(Cache *cache, int index);

#endif