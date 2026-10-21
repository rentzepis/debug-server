/**
 * cache.h - functions in cache.c
 */
#ifndef __CACHE_H__
#define __CACHE_H__

#include "proxy.h"

/* Proxy cache for web pages visited */
typedef struct _cache_object {
    unsigned int recently_use;   // if recently used, it should be set to current max
    int filesize, obj_size;
    char uri[MAXLINE];
    char header[MAXLINE];
    char *file;         // use malloc for a block
}cache_object;
typedef struct _cache {
    int obj_num;
    int available_size;
    cache_object *object[MAX_OBJ_NUM];
}Cache;

void init_cache(Cache* cache);
void forward_cache_content(int clientfd, Cache* cache, 
    cache_object* cache_obj);
void insert_cache(Cache* cache, cache_object* obj);
void delete_cache(Cache* cache);
void* find_in_cache(char* uri, Cache* cache);
void make_new_cache(Cache* cache, char* file, char* uri, 
    char* header, int filesize);

unsigned int timestamp;

#endif