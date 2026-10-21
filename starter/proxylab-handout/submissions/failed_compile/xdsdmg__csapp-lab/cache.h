/* $begin cache.h */
#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* Recommended max cache and object sizes */
// #define MAX_CACHE_SIZE 1049000
#define MAX_CACHE_SIZE 1024 * 1024
#define MAX_OBJECT_SIZE 1024 * 100

typedef struct {
  char uri[MAXLINE]; // tag
  uint8_t obj[MAX_OBJECT_SIZE];
  int64_t clock;
  int is_valid;
  size_t obj_size;
} CacheLine;

typedef struct {
  CacheLine cache_line_list[MAX_CACHE_SIZE / MAX_OBJECT_SIZE];
  int64_t clock;
  int read_cnt;
  sem_t r, w;
} Cache;

void cache_init(Cache *cache);
int reader(Cache *cache, int fd, char uri[MAXLINE]);
void writer(Cache *cache, char uri[MAXLINE], uint8_t obj[MAX_OBJECT_SIZE],
            size_t obj_size);

#endif /* __CACHE_H__ */
/* $end cache.h */
