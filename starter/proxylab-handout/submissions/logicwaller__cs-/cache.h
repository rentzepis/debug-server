/*
 * cache.h - 缓存部分函数
 */
#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* 定义缓存键值对 */
typedef struct cache {
    char uri[MAXLINE];
    char object[MAX_OBJECT_SIZE];
    size_t object_size;
    struct cache *next, *pre;
} cache;

void init_cache();
int cache_find(char *uri, char *res, size_t *size);
void cache_insert(char *uri, char *object, size_t object_size);
void cache_update(cache *now);
size_t cache_delete();
void print_cache();

#endif
