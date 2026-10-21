#ifndef __CACHE_H__
#define __CACHE_H__
#include "csapp.h"
#include <semaphore.h>
#include <stdio.h>
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_SIZE (MAX_CACHE_SIZE / MAX_OBJECT_SIZE)

typedef struct {
  int size;
  int exit;
  int count; // use to LRU
  char url[MAXLINE];
  char *object;
} SingleBlock;

typedef struct {
  int readcnt;
  sem_t mutex, w;
  SingleBlock blocks[CACHE_SIZE]; // Better naming
} CacheBlock;

ssize_t findCache(CacheBlock *cache, char *url, char *buf);
void CacheFree(CacheBlock *cache);
void insertCache(CacheBlock *cache, char *url, char *buf, int webpagesize);
SingleBlock *selectCache(CacheBlock *cache);
void Cacheinit(CacheBlock *cache);

#endif
