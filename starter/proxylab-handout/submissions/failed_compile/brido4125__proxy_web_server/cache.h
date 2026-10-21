//
// Created by 홍창섭 on 2022/11/09.
//
#include "csapp.h"

#ifndef PROXY_WEB_SERVER_CACHE_H
#define PROXY_WEB_SERVER_CACHE_H

#define MAX_CACHE_SIZE      1049000
#define MAX_OBJECT_SIZE     102400
#define MAX_CACHE_COUNT     10

typedef struct CacheNodeType{
    char url[MAX_OBJECT_SIZE];//별자
    char data[MAX_CACHE_SIZE];
    struct CacheNodeType* prev;
    struct CacheNodeType* next;
}CacheNode;

typedef struct CacheListType{
    int currentElementCount;
    CacheNode* frontNode;
    CacheNode* rearNode;
}CacheList;

CacheList * initCache();
void insertCacheNode(CacheList *list, char *url,char* response);
void deleteCache(CacheList *list);
char* findCacheNode(CacheList* list,char* url);

#endif //PROXY_WEB_SERVER_CACHE_H