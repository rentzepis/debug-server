
#include "cache.h"


static pthread_rwlock_t lock;

struct cache_header *cache_init(){
    pthread_rwlock_init(&lock,NULL);
    struct cache_header *new_cache = malloc(sizeof(struct cache_header));
    new_cache->total_size = 0;
    new_cache->start = NULL;
    new_cache->currTime = 0;
    return new_cache;
}

int cache_empty(struct cache_header *cache){
    return cache->total_size == 0;
}

int deleteBlock(struct cache_header *cache){
    struct cache_block *prev = NULL;
    struct cache_block *toRemovePrev = NULL;
    struct cache_block *curr = cache->start;
    struct cache_block *next = curr->next;
    struct cache_block *toRemove = NULL;
    int minTime = 0;

    while(curr != NULL){

        if (curr->timestamp > minTime){
            toRemove = curr;
            next = curr->next;
            toRemovePrev = prev;
        }
        prev = curr;
        curr = curr->next;
    }

    if (prev){
        toRemovePrev->next = next;
    } else{
        cache->start = next;
    }
    int returnVal = toRemove->size;
    cache->total_size -= toRemove->size;
    free(toRemove->data);
    free(toRemove->tag);
    free(toRemove);

    return returnVal;
}

void remove_from_cache(struct cache_header *cache, size_t size){
    int bytesNeeded = size - (MAX_CACHE_SIZE - cache->total_size);
    while (bytesNeeded > 0 && !cache_empty(cache)){
        bytesNeeded -= deleteBlock(cache);
    }
}


void insert_into_cache(struct cache_header *cache, size_t size, char* data, char* tag){
    pthread_rwlock_wrlock(&lock);
    struct cache_block *new_block = malloc(sizeof(struct cache_block));
    if (cache->total_size + size > MAX_CACHE_SIZE) remove_from_cache(cache,size);
    new_block->next = cache->start;
    cache->start = new_block;
    cache->total_size += size;
    new_block->size = size;
    new_block->data = malloc(size);
    memcpy(new_block->data, data, size);
    new_block->tag = malloc(strlen(tag) + 1);
    strcpy(new_block->tag,tag);
    new_block->timestamp = ++cache->currTime;
    Sem_init(&new_block->mutex, 0, 1);
    pthread_rwlock_unlock(&lock);
}


struct cache_block* find_in_cache(struct cache_header *cache, char* tag){
    pthread_rwlock_rdlock(&lock);

    if (cache_empty(cache)) {
        pthread_rwlock_unlock(&lock);
        return NULL;
    }
    struct cache_block* curr = cache->start;

    while (curr != NULL){

        if (strcasecmp(curr->tag,tag) == 0) {
            P(&curr->mutex);
            curr->timestamp = ++cache->currTime;
            V(&curr->mutex);
            pthread_rwlock_unlock(&lock);
            return curr;
        }
        curr = curr->next;
    }
    pthread_rwlock_unlock(&lock);
    return NULL;
}


