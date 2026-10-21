#include "cache.h"
#include "csapp.h"
#include <stdio.h>
#include <string.h>

void Cacheinit(CacheBlock *cache) {
  for (int i = 0; i < CACHE_SIZE; ++i) {
    cache->blocks[i].count = 0;
    cache->blocks[i].exit = 0;
    cache->blocks[i].size = 0;
    memset(cache->blocks[i].url, 0, sizeof(cache->blocks[i].url));
    cache->blocks[i].object = Calloc(MAX_OBJECT_SIZE, sizeof(char));
  }
  cache->readcnt = 0;
  sem_init(&cache->mutex, 0, 1);
  sem_init(&cache->w, 0, 1);
}
void CacheFree(CacheBlock *cache) {
  for (int i = 0; i < CACHE_SIZE; ++i) {
    Free(cache->blocks[i].object);
  }
}
ssize_t findCache(CacheBlock *cache, char *url, char *buf) {
  P(&cache->mutex);
  cache->readcnt++;
  if (cache->readcnt == 1) {
    P(&cache->w);
  }
  V(&cache->mutex);
  int size = -1, find = -1;
  for (int i = 0; i < CACHE_SIZE; ++i) {
    if (cache->blocks[i].exit && !strcmp(url, cache->blocks[i].url)) {
      memcpy(buf, cache->blocks[i].object, cache->blocks[i].size);
      size = cache->blocks[i].size;
      find = i;
      break;
    }
  }
  P(&cache->mutex);
  cache->readcnt--;
  if (find != -1) {
    cache->blocks[find].count = 0;
  }
  for (int i = 0; i < CACHE_SIZE; ++i) {
    cache->blocks[i].count++;
  }
  if (cache->readcnt == 0) {
    V(&cache->w);
  }
  V(&cache->mutex);
  return size;
}

// ssize_t findCache(CacheBlock *cache, char *url, char *buf) {
//   P(&cache->w);
//   int size = -1;
//   for (int i = 0; i < CACHE_SIZE; ++i) {
//     if (cache->blocks[i].exit && !strcmp(url, cache->blocks[i].url)) {
//       memcpy(buf, cache->blocks[i].object, cache->blocks[i].size);
//       size = cache->blocks[i].size;
//       cache->blocks[i].count = 0;
//       continue;
//     }
//     cache->blocks[i].count++;
//   }
//   V(&cache->w);
//   return size;
// }

SingleBlock *selectCache(CacheBlock *cache) {
  int max = -1;
  SingleBlock *block;
  for (int i = 0; i < CACHE_SIZE; ++i) {
    if (cache->blocks[i].exit == 0) {
      return &cache->blocks[i];
    }
    if (cache->blocks[i].count > max) {
      max = cache->blocks[i].count;
      block = &cache->blocks[i];
    }
  }
  return block;
}

void insertCache(CacheBlock *cache, char *url, char *buf, int webpagesize) {
  P(&cache->w);
  SingleBlock *block = selectCache(cache);
  block->count = 0;
  block->exit = 1;
  block->size = webpagesize;
  for (int i = 0; i < CACHE_SIZE; ++i) {
    cache->blocks[i].count++;
  }
  strcpy(block->url, url);
  memcpy(block->object, buf, webpagesize);
  V(&cache->w);
}
