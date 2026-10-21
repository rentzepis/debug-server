#include "cache.h"

#include <pthread.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct cache_entry {
  struct cache_entry *next;
  struct cache_entry *prev;

  ssize_t patience;

  const char *uri;
  char *data;
  size_t size;
};

static struct {
  size_t freesz;

  struct cache_entry head;
  pthread_rwlock_t cache_lock;
} cache;

void cache_init(size_t maxsize) {
  cache.freesz = maxsize;

  cache.head.next = cache.head.prev = &cache.head;
  pthread_rwlock_init(&cache.cache_lock, NULL);
}

static const char *uri_gen(const char *host, const char *loc) {
  char *uri;

  size_t hostlen, loclen;

  hostlen = strlen(host);
  loclen = strlen(loc);

  uri = malloc(hostlen + loclen + 2);
  memcpy(uri, host, hostlen);
  uri[hostlen] = '/';
  memcpy(uri + hostlen + 1, loc, loclen);
  uri[hostlen + loclen + 1] = '\0';

  return uri;
}

int cache_get(const char *host, const char *loc, char **buf, size_t *bufsz) {
  struct cache_entry *node;
  const char *uri;

  uri = uri_gen(host, loc);

  pthread_rwlock_rdlock(&cache.cache_lock);

  for (node = cache.head.next; node != &cache.head; node = node->next)
    if (strcmp(node->uri, uri) == 0)
      break;

  if (node != &cache.head) {
    if (*bufsz < node->size) {
      free(*buf);
      *buf = malloc(node->size);
    }

    *bufsz = node->size;
    memcpy(*buf, node->data, *bufsz);
    __atomic_sub_fetch(&node->patience, 1, __ATOMIC_RELAXED);
  }

  pthread_rwlock_unlock(&cache.cache_lock);

  free((void *)uri); // NOLINT(clang-diagnostic-cast-qual)
  return node != &cache.head;
}

#define DEFAULT_PATIENCE 3

static struct cache_entry *entry_create(const char *host, const char *loc,
                                        const char *data, size_t size) {
  struct cache_entry *entry;

  entry = malloc(sizeof(*entry));

  entry->patience = DEFAULT_PATIENCE;
  entry->uri = uri_gen(host, loc);

  entry->data = malloc(size);
  memcpy(entry->data, data, size);
  entry->size = size;

  return entry;
}

static void entry_destroy(struct cache_entry *entry) {
  free((void *)entry->data);
  free((void *)entry->uri); // NOLINT(clang-diagnostic-cast-qual)
  free(entry);
}

static void cache_insert(struct cache_entry *node) {
  node->next = cache.head.next;
  cache.head.next->prev = node;

  node->prev = &cache.head;
  cache.head.next = node;
}

static void cache_promote(struct cache_entry *node) {
  node->patience += DEFAULT_PATIENCE;

  node->next->prev = node->prev;
  node->prev->next = node->next;

  node->next = cache.head.next;
  node->prev = &cache.head;
  cache.head.next->prev = node;
  cache.head.next = node;
}

static size_t cache_evict(struct cache_entry *node) {
  size_t prev_size;

  node->prev->next = node->next;
  node->next->prev = node->prev;

  prev_size = node->size;
  entry_destroy(node);

  return prev_size;
}

static struct cache_entry *cache_bookkeeping(struct cache_entry *pending) {
  struct cache_entry *node;

  for (node = cache.head.prev; node != &cache.head;) {
    if (strcmp(node->uri, pending->uri) == 0) {
      cache_promote(node);
      node->patience = DEFAULT_PATIENCE;
      return node;
    }

    node = node->prev;

    if (node->next->patience <= 0)
      cache_promote(node->next);
    else if (cache.freesz < pending->size)
      cache.freesz += cache_evict(node->next);
  }

  while (cache.freesz < pending->size) {
    for (node = cache.head.prev; node != &cache.head;) {
      node = node->prev;
      if (node->next->patience <= 0)
        cache_promote(node->next);
      else
        cache.freesz += cache_evict(node->next);
    }
  }

  return NULL;
}

void cache_put(const char *host, const char *loc, const char *buf,
               size_t bufsz) {
  struct cache_entry *node, *found;

  node = entry_create(host, loc, buf, bufsz);

  pthread_rwlock_wrlock(&cache.cache_lock);

  found = cache_bookkeeping(node);
  if (found != NULL)
    goto exit_unlock;

  cache.freesz -= bufsz;
  cache_insert(node);

exit_unlock:
  pthread_rwlock_unlock(&cache.cache_lock);

  if (found != NULL)
    entry_destroy(node);
}
