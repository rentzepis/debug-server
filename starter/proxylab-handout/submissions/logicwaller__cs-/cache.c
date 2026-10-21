/* 
 * cache.c - 缓存部分的函数实现，最近一次访问的放在最后
 */

#include "cache.h"

/* 初始化缓存 */
static cache *cache_head;
static size_t total_size = 0; //记录当前缓存总大小

/* 线程同步初始化 */
static sem_t mutex, w;
static int readcnt;

/*
 * init_cache - 初始化
 */
void init_cache(){
    /* 初始化cache_head */
    cache_head = Malloc(sizeof(cache));
    cache_head -> uri[0] = '\0';
    cache_head -> next = NULL;
    cache_head -> pre = NULL;

    /* 线程同步初始化 */
    Sem_init(&mutex, 0, 1);
    Sem_init(&w, 0, 1);
    readcnt = 0;
}


/*
 * cache_find - 在cache里寻找uri对应的cache。找到返回1，结果存入res；否则返回0
 */
int cache_find(char *uri, char *res, size_t *size){
    P(&mutex);
    readcnt++;
    if(readcnt == 1){
        P(&w);
    }
    V(&mutex);

    cache *now = cache_head;
    int istrue = 0;

    while(now != NULL){
        if(strcmp(now -> uri, uri) == 0){
            memcpy(res, now -> object, now -> object_size);
            *size = now -> object_size;

            P(&mutex);
            cache_update(now);
            V(&mutex);

            istrue = 1;
            break;
        }
        now = now -> next;
    }

    P(&mutex);
    readcnt--;
    if(readcnt == 0){
        V(&w);
    }
    V(&mutex);

    return istrue;
}

/*
 * cache_insert - 在cache里插入object
 */
void cache_insert(char *uri, char *object, size_t object_size){//??
    P(&w);

    total_size += object_size;
    while(total_size > MAX_CACHE_SIZE){
        total_size -= cache_delete();
    }

    cache *now = cache_head;
    
    while(now -> next != NULL){// 获取最后一个cache
        now = now -> next;
    } 

    cache *new = Malloc(sizeof(cache));
    memset(new, 0, sizeof(cache));

    now -> next = new;
    new -> pre = now;
    new -> next = NULL;

    strcpy(new->uri, uri);
    memcpy(new -> object, object, object_size);
    new -> object_size = object_size;

    V(&w);
}

/*
 * cache_update - 将now放在当前cache的最后
 */
void cache_update(cache *now){
    /* 将now从cache中剔除 */
    if (now -> next != NULL) now -> next -> pre = now -> pre;
    else return; //若now的next为空，则说明now为末尾，直接返回即可
    if (now -> pre != NULL) now -> pre -> next = now -> next;
    
    cache *tem = cache_head;
    while(tem -> next != NULL){// 获取最后一个cache
        tem = tem -> next;
    }

    /* 将now插入最后一个 */
    tem -> next = now;
    now -> pre = tem;
    now -> next = NULL;
}

/*
 * cache_delete - 从cache中删除第一个有效cache
 */
size_t cache_delete(){
    cache *first = cache_head -> next;
    size_t size;
    if(first == NULL) return 0;

    size = first -> object_size;
    cache_head -> next = first -> next;
    if(first -> next != NULL) first -> next -> pre = cache_head; 
    free(first);
    return size;
}

/*
 * print_cache - 输出所有的cache的键（调试用）
 */
void print_cache(){
    cache *now = cache_head;
    int cnt = 1;
    while(now != NULL){
        printf("cache %d:\n", cnt++);
        printf("uri:%s\n", now -> uri);
        now = now -> next;
    }
}