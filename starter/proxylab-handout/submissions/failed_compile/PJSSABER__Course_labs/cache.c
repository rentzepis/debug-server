#include <stdio.h>
#include "csapp.h"
#include <stdbool.h>
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


/*
    implement a thread-safe circular queue
    Not a LRU
    and its method.
    其实可能用hash+linked-list方式会更好，锁更小 Or ?? 再想想 这种链表的锁好像不是那么简单啊！！
    读写锁在缓存里面好像至关重要啊~~~~ 如何处理饿死？ 放一个semaphore reader P writer V 或者 reader内部一个生产者消费者
*/

typedef struct cache_block {  
    char key[MAXLINE]; 
    char val[MAX_OBJECT_SIZE + 20];
} cache_block;

const static int cache_size = 10;

typedef struct cache {  
    cache_block buff[10];
    int head, tail, size; // [head, tail]; when size == 0 (tail < head), it's empty
    int read_cnt;
    sem_t mutex, w;
} cache;

cache* init() {
    cache *ptr = (cache*)malloc(sizeof(cache));   // put cache in the heap so that all threads can access it easily
    if (ptr == NULL) {
        exit(1);
    }

    (*ptr).head = 0;
    (*ptr).tail = -1;
    (*ptr).size = 0;
    (*ptr).read_cnt = 0;
    Sem_init(&(*ptr).mutex, 1, 1);
    Sem_init(&(*ptr).w, 1, 1);
    return ptr;
}

void reader_lock(cache *ptr) {
    P(&(*ptr).mutex);
    (*ptr).read_cnt += 1;
    if((*ptr).read_cnt == 1) {
        P(&(*ptr).w);
    }
    V(&(*ptr).mutex);
    return;
}

void reader_unlock(cache *ptr) {
    P(&(*ptr).mutex);
    (*ptr).read_cnt -= 1;
    if((*ptr).read_cnt == 0) {
        V(&(*ptr).w);
    }
    V(&(*ptr).mutex);
    return;
}

void writer_lock(cache *ptr) {
    P(&(*ptr).w);
}

void writer_unlock(cache *ptr) {
    V(&(*ptr).w);
}

/*
    return NULL if missed
    else return target cache 
*/
char* check_cache(char *target, cache *ptr) {
    char *ret = NULL;
    reader_lock(ptr);
    for (int i = 0; i < (*ptr).size; i++) {    // using a hashmap can save time here. 但是一把大锁还是需要的
        int idx = ((*ptr).head + i) % cache_size;
        if (strcmp(target, (*ptr).buff[idx].key) == 0) {     
            ret = (*ptr).buff[idx].val;
            break;
        }
    }
    reader_unlock(ptr);
    return ret;
}

/*
    drop a cache
    important: this function must called when holding a writer_lock!!!!

*/
void pop_cache(cache *ptr) {
    int idx = ((*ptr).head + 1) % cache_size;
    (*ptr).head = idx;
    (*ptr).size -= 1;
}

/*
    put a cache_block in the cache
*/
void push_cache(char *key, char *val, cache *ptr) {
    writer_lock(ptr);
    if ((*ptr).size == cache_size) {
        pop_cache(ptr);
    }
    // notice here (*ptr).size < cache_size
    (*ptr).tail = ((*ptr).tail + 1) % cache_size;
    strcpy((*ptr).buff[(*ptr).tail].key, key);
    strcpy((*ptr).buff[(*ptr).tail].val, val);
    (*ptr).size += 1;
    writer_unlock(ptr);
    printf("############################## put in cache ####################");
    printf("%s", (*ptr).buff[(*ptr).tail].val);
}