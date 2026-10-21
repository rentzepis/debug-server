#include "cache.h"
#include <limits.h>

void cache_init(Cache *cache) {
  cache->read_cnt = 0;
  Sem_init(&cache->r, 0, 1);
  Sem_init(&cache->w, 0, 1);

  for (int i = 0; i < MAX_CACHE_SIZE / MAX_OBJECT_SIZE; i++) {
    cache->cache_line_list[i].is_valid = 0;
    cache->cache_line_list[i].clock = 0;
  }
}

int reader(Cache *cache, int fd, char uri[MAXLINE]) {
  int in_cache = 0;

  P(&cache->r);
  cache->clock++;
  cache->read_cnt++;
  if (cache->read_cnt == 1)
    P(&cache->w);
  V(&cache->r);

  for (int i = 0; i < MAX_CACHE_SIZE / MAX_OBJECT_SIZE; i++) {
    if (cache->cache_line_list[i].is_valid == 0) {
      continue;
    }

    if (!strcmp(cache->cache_line_list[i].uri, uri)) {
      cache->cache_line_list[i].clock = cache->clock;
      Rio_writen(fd, cache->cache_line_list[i].obj,
                 cache->cache_line_list[i].obj_size);
      in_cache = 1;
      break;
    }
  }

  P(&cache->r);
  cache->read_cnt--;
  if (cache->read_cnt == 0)
    V(&cache->w);
  V(&cache->r);

  return in_cache;
}

void writer(Cache *cache, char uri[MAXLINE], uint8_t obj[MAX_OBJECT_SIZE],
            size_t obj_size) {
  P(&cache->w);
  cache->clock++;

  int index = -1;

  for (int i = 0; i < MAX_CACHE_SIZE / MAX_OBJECT_SIZE; i++) {
    if (cache->cache_line_list[i].is_valid == 0) {
      index = i;
      break;
    }
  }

  if (index < 0) {
    index = 0;
    int64_t min_clock = INT_MAX;

    for (int i = 0; i < MAX_CACHE_SIZE / MAX_OBJECT_SIZE; i++) {
      if (min_clock > cache->cache_line_list[i].clock) {
        min_clock = cache->cache_line_list[i].clock;
        index = i;
      }
    }
  }

  strcpy(cache->cache_line_list[index].uri, uri);
  for (int i = 0; i < obj_size; i++)
    cache->cache_line_list[index].obj[i] = obj[i];

  cache->cache_line_list[index].is_valid = 1;
  cache->cache_line_list[index].clock = cache->clock;
  cache->cache_line_list[index].obj_size = obj_size;

  V(&cache->w);
}
