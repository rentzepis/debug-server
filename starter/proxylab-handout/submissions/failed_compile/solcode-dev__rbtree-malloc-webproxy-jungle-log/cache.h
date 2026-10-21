#ifndef CACHE_H
#define CACHE_H

#include <time.h>
#include <pthread.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define FILE_NAME_SIZE 4096

// canAddToCache() 반환값
#define CACHE_OK 0
#define CACHE_FILE_TOO_LARGE 1  // 파일이 MAX_OBJECT_SIZE 초과
#define CACHE_EVICTION_NEEDED 2 // LRU 삭제 필요
#define CACHE_REALLOC_NEEDED 3  // 배열 크기 확장 필요

typedef struct
{
  char *host;
  char *uri;
  char *filePath;
  size_t fileSize;
  time_t timestamp;
} CacheEntry;

// 전역 변수 선언
extern int slot_allocated;
extern int slot_used;   // 사용된 배열 슬롯
extern int cached_size; // 캐시 전체 크기
extern CacheEntry *cache;
extern pthread_mutex_t cache_mutex;

// 함수 선언
void cache_init();
int canAddToCache(char *host, char *uri, size_t filesize);
void add_cache_entry(char *host, char *uri, size_t filesize);
int do_realloc(char *host, char *uri, size_t filesize);
int delete_LRU();
int isInCache(char *host, char *uri);

#endif // CACHE_H