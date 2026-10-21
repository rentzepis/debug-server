/* Recommended max cache and object sizes */
#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct{
    int readcnt;
    sem_t mutex;
    sem_t writeLock;
} rwLock_t;

void rwLock_init(rwLock_t *rwLock);
void rwLock_RLock(rwLock_t *rwLock);
void rwLock_RUnlock(rwLock_t *rwLock);
void rwLock_Lock(rwLock_t *rwLock);
void rwLock_Unlock(rwLock_t *rwLock);

typedef struct{
    char *key;
    char *value;
    int valSize;
    struct cache_node_t *prev;
    struct cache_node_t *next;
} cache_node_t;

typedef struct{
    int capacity;
    int size;
    cache_node_t *head;
    cache_node_t *tail;
} lru_cache_t;


void LRUCache_init(lru_cache_t *lru);
void LRUCache_free(lru_cache_t *lru);
void LRUCache_put(lru_cache_t *lru, char *key, char *value, int valSize);
void LRUCache_removeHead(lru_cache_t *lru);
cache_node_t *LRUCache_get(lru_cache_t *lru, char *key);

#endif