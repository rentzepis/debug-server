#include "csapp.h"
#include "cache.h"

int slot_used = 0;   // 사용된 배열 슬롯
int cached_size = 0; // 캐시 전체 크기
int slot_allocated = 0;
CacheEntry *cache = NULL;
pthread_mutex_t cache_mutex = PTHREAD_MUTEX_INITIALIZER;

void cache_init()
{
  printf("\n✅ ================ Cache initialized. ================ \n");

  slot_allocated = 10;
  cache = malloc(sizeof(CacheEntry) * slot_allocated);

  if (cache == NULL)
  {
    printf("%d ❌ [cache_init] malloc failed\n", __LINE__);
    exit(1);
  }

  memset(cache, 0, sizeof(CacheEntry) * slot_allocated);
  printf("%d ♻️ [cache_init] slot_allocated= %d\n", __LINE__, slot_allocated);
}

int isInCache(char *host, char *uri)
{
  printf("%d ♻️ [isInCache] called: host=%s, uri=%s, slot_used=%d\n", __LINE__, host, uri, slot_used);

  for (int i = 0; i < slot_used; i++)
  {
    if (strstr(cache[i].host, host) && strstr(cache[i].uri, uri))
    {
      printf("%d ♻️ [isInCache] HIT: index=%d\n", __LINE__, i);

      return 1;
    }
  }
  printf("%d ♻️ [isInCache] MISS\n", __LINE__);

  return 0;
}

int canAddToCache(char *host, char *uri, size_t filesize)
{
  printf("%d ♻️ [canAddToCache] called: host=%s, uri=%s, filesize=%zu\n", __LINE__, host, uri, filesize);

  // 1. 파일최대크기 < 실제 파일 크기 ?
  if (MAX_OBJECT_SIZE < filesize)
  {
    printf("%d ♻️ [canAddToCache] file too large: filesize=%zu\n", __LINE__, filesize);
    return CACHE_FILE_TOO_LARGE; // 파일 캐싱 거부
  }

  // 2. 전체캐시 최대크기 < 파일 크기 + 현재 캐시 크기 ?
  if (MAX_CACHE_SIZE < filesize + cached_size)
  {
    printf("%d ♻️ [canAddToCache] cache eviction needed: cached_size=%d, filesize=%zu\n", __LINE__, cached_size, filesize);

    delete_LRU();
    return CACHE_EVICTION_NEEDED;
  }

  // 3. 사용한 슬롯수 == 배열 크기 ?
  if (slot_used == slot_allocated)
  {
    printf("%d ♻️ [canAddToCache] realloc needed: slot_used=%d, slot_allocated=%d\n", __LINE__, slot_used, slot_allocated);

    do_realloc(host, uri, filesize);
    add_cache_entry(host, uri, filesize);
    return CACHE_REALLOC_NEEDED;
  }

  printf("%d ♻️ [canAddToCache] cache ok\n", __LINE__);
  return CACHE_OK;
}

void add_cache_entry(char *host, char *uri, size_t filesize)
{
  printf("%d ♻️ [add_cache_entry] called: host=%s, uri=%s, filesize=%zu\n", __LINE__, host, uri, filesize);

  pthread_mutex_lock(&cache_mutex);
  printf("%d ♻️ [add_cache_entry] mutex locked\n", __LINE__);

  cache[slot_allocated - 1].host = host;
  cache[slot_allocated - 1].uri = uri;
  cache[slot_allocated - 1].fileSize = filesize;
  cache[slot_allocated - 1].timestamp = time(NULL);

  printf("%d ♻️ [add_cache_entry] entry added at index %d\n", __LINE__, slot_allocated - 1);

  pthread_mutex_unlock(&cache_mutex);
  printf("%d ♻️ [add_cache_entry] mutex unlocked\n", __LINE__);

  slot_used += 1;
  cached_size += filesize;
  printf("%d ♻️ [add_cache_entry] slot_used=%d, cached_size=%d\n", __LINE__, slot_used, cached_size);
}

int do_realloc(char *host, char *uri, size_t filesize) // NOTE: size는 헤더 + 바디 사이즈
{
  printf("%d ♻️ [do_realloc] called: host=%s, uri=%s, filesize=%zu\n", __LINE__, host, uri, filesize);

  pthread_mutex_lock(&cache_mutex);
  printf("%d ♻️ [do_realloc] mutex locked\n", __LINE__);

  CacheEntry *new_cache = realloc(cache, sizeof(CacheEntry));
  if (new_cache == NULL)
  {
    printf("%d ❌ [do_realloc] realloc failed\n", __LINE__);
    pthread_mutex_unlock(&cache_mutex);
    return -1;
  }
  cache = new_cache;
  slot_allocated += 1;

  printf("%d ♻️ [do_realloc] cache reallocated: slot_allocated=%d\n", __LINE__, slot_allocated);

  pthread_mutex_unlock(&cache_mutex);
  printf("%d ♻️ [do_realloc] mutex unlocked\n", __LINE__);
}

/* 가장 오래된 캐시 찾아서 메모리 해제 (배열 슬롯은 유지한다) */
int delete_LRU()
{
  printf("%d ♻️ [delete_LRU] called\n", __LINE__);

  /* 가장 오래된 요소의 index 찾기 */
  int iru_index = 0;
  time_t oldest = cache[0].timestamp;

  pthread_mutex_lock(&cache_mutex);
  printf("%d ♻️ [delete_LRU] mutex locked\n", __LINE__);

  for (int i = 0; i < slot_used; i++)
  {
    if (cache[i].timestamp < oldest)
    {
      oldest = cache[i].timestamp;
      iru_index = i;
    }
  }
  printf("%d ♻️ [delete_LRU] LRU index=%d, oldest=%ld\n", __LINE__, iru_index, oldest);

  cached_size -= cache[iru_index].fileSize;
  slot_used -= 1;
  printf("%d ♻️ [delete_LRU] slot_used=%d, cached_size=%d\n", __LINE__, slot_used, cached_size);

  /* 삭제할 항목의 메모리 해제 */
  free(cache[iru_index].filePath);
  free(cache[iru_index].fileSize);
  free(cache[iru_index].host);
  free(cache[iru_index].uri);
  free(cache[iru_index].timestamp);

  pthread_mutex_unlock(&cache_mutex);
  printf("%d ♻️ [delete_LRU] mutex unlocked\n", __LINE__);

  return iru_index;
}