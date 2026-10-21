#ifndef __CACHE_H__
#define __CACHE_H__
#include <stddef.h>

typedef struct cache_block {
  char *uri;
  char *object;
  size_t size;
  struct cache_block *prev;
  struct cache_block *next;
} cache_block;

void init_cache(size_t max_cache_size, size_t max_object_size);

int insert_object(const char *uri, const char *buf, size_t buflen);

void find_cache(const char *uri, char **buf, size_t *buflen);

void free_cache();

#endif