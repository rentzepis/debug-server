#include "cache.h"

#include <assert.h>

#include "csapp.h"

cache_block *block_head;
size_t MAX_CACHE_SIZE;
size_t MAX_OBJECT_SIZE;
size_t size;

pthread_rwlock_t rwlock;

void init_cache(size_t max_cache_size, size_t max_object_size) {
  MAX_CACHE_SIZE = max_cache_size;
  MAX_OBJECT_SIZE = max_object_size;
  size = 0;
  block_head = malloc(sizeof(cache_block));
  block_head->next = block_head;
  block_head->prev = block_head;
  pthread_rwlock_init(&rwlock, NULL);
}

void free_cache() {
  pthread_rwlock_wrlock(&rwlock);
  cache_block *iter = block_head->next;
  while (iter != block_head) {
    free(iter->uri);
    free(iter->object);
    cache_block *tmp = iter;
    iter = iter->next;
    free(tmp);
  }
  free(block_head);
  pthread_rwlock_destroy(&rwlock);
}

cache_block *set_block(const char *uri, size_t uri_len, const char *buf,
                       size_t buf_len) {
  cache_block *block = malloc(sizeof(cache_block));
  block->uri = malloc(uri_len + 1);
  memcpy(block->uri, uri, uri_len);
  block->uri[uri_len] = '\0';
  block->object = malloc(buf_len);
  memcpy(block->object, buf, buf_len);
  block->size = buf_len;
  return block;
}

void insert(cache_block *pos, cache_block *block) {
  block->next = pos;
  block->prev = pos->prev;
  pos->prev = block;
  block->prev->next = block;
}

void move_to_head(cache_block *block) {
  block->prev->next = block->next;
  block->next->prev = block->prev;
  insert(block_head->next, block);
}

void free_tail() {
  assert(block_head->prev != block_head);
  cache_block *ptr = block_head->prev;
  ptr->prev->next = ptr->next;
  ptr->next->prev = ptr->prev;
  free(ptr->uri);
  free(ptr->object);
  size -= ptr->size;
  free(ptr);
}

int insert_object(const char *uri, const char *buf, size_t buflen) {
  pthread_rwlock_wrlock(&rwlock);
  cache_block *iter = block_head->next;
  for (; iter != block_head; iter = iter->next) {
    if (strcmp(uri, iter->uri)) continue;
    move_to_head(iter);
    pthread_rwlock_unlock(&rwlock);
    return 0;
  }
  while (size + buflen > MAX_CACHE_SIZE) {
    free_tail();
  }
  insert(block_head->next, set_block(uri, strlen(uri), buf, buflen));
  size += buflen;
  pthread_rwlock_unlock(&rwlock);
  return 0;
}

void find_cache(const char *uri, char **buf, size_t *buflen) {
  pthread_rwlock_rdlock(&rwlock);
  cache_block *iter = block_head->next;
  for (; iter != block_head; iter = iter->next) {
    if (strcmp(uri, iter->uri)) continue;
    *buf = iter->object;
    *buflen = iter->size;
    move_to_head(iter);
    pthread_rwlock_unlock(&rwlock);
    return;
  }
  *buflen = 0;
  *buf = NULL;
  pthread_rwlock_unlock(&rwlock);
}