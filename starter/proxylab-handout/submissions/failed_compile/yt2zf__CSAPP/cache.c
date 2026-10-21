#include "cache.h"

rwLock_t rwLock_global; /* 读写锁，用于cache的get和put*/

void rwLock_init(rwLock_t *rwLock){
    rwLock->readcnt = 0;
    Sem_init(&rwLock->mutex, 0, 1);
    Sem_init(&rwLock->writeLock, 0, 1);
}

void rwLock_RLock(rwLock_t *rwLock){
    P(&rwLock->mutex);
    rwLock->readcnt++;
    if (rwLock->readcnt == 1){
        P(&rwLock->writeLock);
    }
    V(&rwLock->mutex);
}

void rwLock_RUnlock(rwLock_t *rwLock){
    P(&rwLock->mutex);
    rwLock->readcnt--;
    if (rwLock->readcnt == 0){
        V(&rwLock->writeLock);
    }
    V(&rwLock->mutex);
}

void rwLock_Lock(rwLock_t *rwLock){
    P(&rwLock->writeLock);
}

void rwLock_Unlock(rwLock_t *rwLock){
    V(&rwLock->writeLock);
}


void LRUCache_init(lru_cache_t *lru){
    lru->capacity = MAX_CACHE_SIZE;
    lru->size = 0;
    lru->head = NULL;
    lru->tail = NULL;
}

void LRUCache_free(lru_cache_t *lru){
    cache_node_t *curNode = lru->head;
    while (curNode != NULL){
        cache_node_t *tmp = curNode->next;
        Free(curNode->key);
        Free(curNode->value);
        Free(curNode);
        curNode = tmp;
    }
}

void LRUCache_removeHead(lru_cache_t *lru){
    if (lru->head == NULL){
        return;
    }

    if (lru->head == lru->tail){
        lru->size = 0;
        Free(lru->head->key);
        Free(lru->head->value);
        Free(lru->head);
        lru->head = NULL;
        lru->tail = NULL;
        return;
    }

    cache_node_t *headNode = lru->head;
    lru->head = headNode->next;
    lru->head->prev = headNode->prev;
    if (lru->head->next == NULL){
        lru->tail = lru->head;
    }
    lru->size -= headNode->valSize;
    Free(headNode->key);
    Free(headNode->value);
    Free(headNode);
}

void LRUCache_put(lru_cache_t *lru, char *key, char *value, int valSize){
    rwLock_Lock(&rwLock_global);
    while (lru->size + valSize > lru->capacity){
        LRUCache_removeHead(lru);
    }
    cache_node_t *newNode = Malloc(sizeof(cache_node_t));
    newNode->key = Malloc(strlen(key) + 1);
    strncpy(newNode->key, key, strlen(key));
    newNode->key[strlen(key)] = '\0';

    newNode->valSize = valSize;
    newNode->value = Malloc(valSize);
    memcpy(newNode->value, value, valSize);

    newNode -> next = NULL;
    if (lru->tail == NULL){
        lru->tail = newNode;
        lru->head = newNode;
        newNode->prev = NULL;
    } else{
        lru->tail->next = newNode;
        newNode->prev = lru->tail;
        lru->tail = newNode;
    }
    lru->size += valSize;
    rwLock_Unlock(&rwLock_global);
}

cache_node_t *LRUCache_get(lru_cache_t *lru, char *key){
    rwLock_RLock(&rwLock_global);

    cache_node_t *curNode = lru->head;
    while (curNode != NULL){
        if (!strcmp(curNode->key, key)){
            return curNode;
        }
        curNode = curNode->next;
    }
    rwLock_RUnlock(&rwLock_global);
    return NULL;
}
