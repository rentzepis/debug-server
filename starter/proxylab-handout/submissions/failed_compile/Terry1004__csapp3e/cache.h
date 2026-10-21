/* $begine cache.h */
#ifndef __CACHE_H__
#define __CACHE_H__

#include <semaphore.h>

typedef struct lru_item_t
{
    char *key;
    char *resp;
    struct lru_item_t *next;
    struct lru_item_t *prev;
    int size;
} lru_item_t;

typedef struct
{
    lru_item_t *head;
    lru_item_t *tail;
    int size;
    int max_size;
    sem_t mutex;
} lru_t;

void lru_init(lru_t *lrup, int max_size);
void lru_destroy(lru_t *lrup);
char *lru_get(lru_t *lrup, char *key, int *size);
int lru_put(lru_t *lrup, char *key, char *resp, int size);

#endif /* __CACHE_H__ */
       /* $end cache.h */