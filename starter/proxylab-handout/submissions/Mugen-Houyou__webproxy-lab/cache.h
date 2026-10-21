#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#include <signal.h>
#include <assert.h>

#define MAX_CACHE_SIZE (1<<20) // 1메가
#define MAX_OBJECT_SIZE (100<<10) // 100킬로

// #define HASH_SIZE 9029 // 실사용 시는 큰 숫자
#define HASH_SIZE 13 // 소수로 충돌 최소화
#define HASH_VAL 5381l // 소수로 충돌 최소화

// 하나의 캐시 객체
typedef struct cache_entry {
    char uri[MAXLINE]; // 캐시된 요청 URI (key)
    //char content[MAX_OBJECT_SIZE]; // 실제 데이터 ==> 이 사이즈 때문에 스택메모리에 넣지 말 것.
    char* content; // 실제 데이터 ==> 이 사이즈 때문에 스택메모리에 넣지 말 것.
    int content_length;

    struct cache_entry* prev; // LRU 이전 노드
    struct cache_entry* next; // LRU 다음 노드
    struct cache_entry* h_next; // 해시 테이블 내 체이닝
} cache_entry_t;

// 캐시 전체 구조
typedef struct {
    cache_entry_t* head; // LRU 리스트의 head (가장 최근)
    cache_entry_t* tail; // LRU 리스트의 tail (가장 오래됨)

    cache_entry_t* hashtable[HASH_SIZE];  // 해시 버킷 - 이 사이즈 때문에 스택메모리에 넣지 말 것.
    size_t total_cached_bytes; // 현재 총 캐시된 바이트 수

    pthread_rwlock_t ptrwlock; // 동시 접근 제어 (read-write lock)
} cache_t;

// === 캐시 관련 API ===
void cache_init(cache_t* cache); 
void cache_deinit(cache_t* cache); // 캐시 전체의 메모리 해제
cache_entry_t* cache_lookup(cache_t* cache, const char* uri, const int use_lock, const int update_lru);  // O(1) 탐색 - TODO: pthread_rwlock_unlock() 어디서 할지 나중에 결정할 것!
void cache_insert_unmanaged(cache_t* cache, const char* uri, const char* buf, int size); // 삽입
void cache_evict_policy_unmanaged(cache_t* cache, int required_size); // 필요시 LRU 제거
inline int cache_size(cache_t* cache); // 현재 총 캐시 바이트 수
void cache_remove(cache_t* cache, const char* uri); // 명시적 삭제 - URI로
void cache_remove_by_entry_unmanaged(cache_t* cache, cache_entry_t* entry); // 명시적 삭제 - cache_entry_t로
void debug_print_cache(cache_t* cache); // LRU 순서대로 출력 (디버깅)
// 이하 함수들의 주석은 cache.c 참조.
int cache_get(cache_t *cache, const char *uri, char *buf_out, int *size_out);
void cache_put(cache_t *cache, const char *uri, const char *buf, int size);

#endif /* __CACHE_H__ */
