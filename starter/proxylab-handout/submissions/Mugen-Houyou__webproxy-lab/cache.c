#include "csapp.h"
#include "cache.h"
#include <pthread.h>

/* 전역 상태 */
// (빔)

/* 유틸부 */
/**
 * hash_uri - djb2 알고리즘으로 int형 해시값 반환
 * 출처: https://stackoverflow.com/questions/64699597/how-to-write-djb2-hashing-function-in-c
 */
static int hash_uri(const char* uri) {
    unsigned long hash = HASH_VAL;
    for (int c = *uri++; c != '\0'; c = *uri++)
        hash = ((hash << 5) + hash) + c;
    return hash % HASH_SIZE;
}

/**
 * move_to_front - 해당 캐시를 맨 앞으로
 * 중요! 락은 여기서 관리되지 않음!
 */
static void move_to_front_unmanaged(cache_t* cache, cache_entry_t* entry) {
    // 얼리 리턴 - 이미 맨 앞이면 
    if (cache->head == entry) 
        return;  

    // 1. 기존 위치에서 entry 제거
    if (entry->prev)
        entry->prev->next = entry->next;
    else
        cache->head = entry->next;

    if (entry->next)
        entry->next->prev = entry->prev;
    else
        cache->tail = entry->prev;

    // 2. entry를 head로 삽입
    entry->prev = NULL;
    entry->next = cache->head;

    if (cache->head)
        cache->head->prev = entry;
    cache->head = entry;

    if (cache->tail == NULL)// 리스트에 아무것도 없다
        cache->tail = entry; 
}

/**
 * evict_lru - 해당 캐시를 퇴출
 * 중요! 락은 여기서 관리되지 않음!
 */
static void evict_lru_unmanaged(cache_t* cache) {
    cache_entry_t* entry = cache->tail;
    if (entry)
        cache_remove_by_entry_unmanaged(cache, entry);
}


/* 구현부 */
/**
 * cache_init - 캐시 전체 체계 초기화
 */
void cache_init(cache_t* cache) {
    cache->head = NULL;
    cache->tail = NULL;
    cache->total_cached_bytes = 0;
    memset(cache->hashtable, 0, sizeof(cache->hashtable)); // 해당 포인터에서 sizeof(cache->hashtable)만큼을 0(NULL)로 초기화.
    pthread_rwlock_init(&cache->ptrwlock, NULL);
}

/**
 * cache_deinit - 캐시 전체 체계 말소 (락 포함)
 */
void cache_deinit(cache_t *cache) {
    pthread_rwlock_wrlock(&cache->ptrwlock);

    cache_entry_t *curr = cache->head;
    while (curr) {
        cache_entry_t *next = curr->next;
        free(curr->content);
        free(curr);
        curr = next;
    }

    cache->head = NULL;
    cache->tail = NULL;
    cache->total_cached_bytes = 0;
    memset(cache->hashtable, 0, sizeof(cache->hashtable));

    pthread_rwlock_unlock(&cache->ptrwlock);
    pthread_rwlock_destroy(&cache->ptrwlock);
}

/**
 * cache_get - 캐시에서 URI에 해당하는 객체를 찾고, 존재할 경우 buf_out에 복사함
 * 내부적으로 락을 획득하며, LRU 업데이트도 수행함.
 * 
 * @param uri 요청한 URI
 * @param buf_out 캐시된 콘텐츠가 복사될 버퍼
 * @param size_out 콘텐츠 길이
 * @return 성공(1), 실패(0)
 */
int cache_get(cache_t *cache, const char *uri, char *buf_out, int *size_out){
    cache_entry_t* entry;

    // Read lock으로 lookup만 진행
    pthread_rwlock_rdlock(&cache->ptrwlock);
    entry = cache_lookup(cache, uri, 0, 0); // NO internal lock, NO LRU 업데이트.
    if (!entry) {
        pthread_rwlock_unlock(&cache->ptrwlock);
        return 0;
    }

    // 콘텐츠 복사
    memcpy(buf_out, entry->content, entry->content_length);
    *size_out = entry->content_length;
    pthread_rwlock_unlock(&cache->ptrwlock);

    // LRU 이동은 별도의 wrlock에서 수행
    // 락 승격 문제는 없지만, rdlock해제→wrlock획득 이 사이에 내용이 바뀌면 문제 소지는 있음
    pthread_rwlock_wrlock(&cache->ptrwlock);
    move_to_front_unmanaged(cache, entry);
    pthread_rwlock_unlock(&cache->ptrwlock);

    return 1; // 찾으면 1
}
int cache_get_v1(cache_t *cache, const char *uri, char *buf_out, int *size_out){
    pthread_rwlock_wrlock(&cache->ptrwlock);
    cache_entry_t* entry = cache_lookup(cache, uri, 0, 0); // LRU 업데이트도 cache_get에서 직접.
    int result = 0; // 1 찾음; 0 없음.

    if(entry){
        memcpy(buf_out, entry->content, entry->content_length);
        *size_out = entry->content_length;
        move_to_front_unmanaged(cache, entry);  
        result = 1;
    }

    pthread_rwlock_unlock(&cache->ptrwlock);
    return result;
}

/**
 * cache_put - 캐시에 새 객체 저장
 * 내부에서 직접 읽기 락 사용!
 * 
 * @param cache: 캐시 포인터
 * @param uri: 요청 URI (key)
 * @param buf: 응답 본문 (payload)
 * @param size: 응답 본문 크기
 * @return void
 */
void cache_put(cache_t *cache, const char *uri, const char *buf, int size) {
    if (size > MAX_OBJECT_SIZE)
        return;

    pthread_rwlock_wrlock(&cache->ptrwlock);
    cache_insert_unmanaged(cache, uri, buf, size);
    pthread_rwlock_unlock(&cache->ptrwlock);
}

/**
 * cache_insert_unmanaged - buf를 기반으로 새 캐시 생성, 리스트 갱신
 * 중요! 반드시 외부에서 락 관리!
 * 
 * @param cache: 캐시 포인터
 * @param uri: 요청 URI (key)
 * @param buf: 응답 본문 (payload)
 * @param size: 응답 본문 크기
 * @return void
 */
void cache_insert_unmanaged(cache_t* cache, const char* uri, const char* buf, int size){
    // 얼리 리턴 - 사이즈 맞는 경우만
    if (size > MAX_OBJECT_SIZE)
        return;

    // 이전꺼 있으면 삭제
    cache_entry_t* entry = cache_lookup(cache, uri, 0, 0);
    if (entry != NULL){
        cache_remove_by_entry_unmanaged(cache, entry);
        entry = NULL;
    }
        
    // 퇴출 정책
    cache_evict_policy_unmanaged(cache, size);
    
    // 새 객체 생성
    cache_entry_t* new_entry = Malloc(sizeof(cache_entry_t));
    strcpy(new_entry->uri, uri);
    new_entry->content = Malloc(size);
    memcpy(new_entry->content, buf, size);
    new_entry->content_length = size;
    new_entry->prev = NULL;
    new_entry->next = NULL;
    new_entry->h_next = NULL;
    
    // 이중 연결 리스트
    new_entry->next = cache->head;
    if (cache->head)
        cache->head->prev = new_entry;
    cache->head = new_entry;
    if (cache->tail == NULL)
        cache->tail = new_entry;

    // 해시 테이블 체이닝
    int hashed_index = hash_uri(uri);
    new_entry->h_next = cache->hashtable[hashed_index];
    cache->hashtable[hashed_index] = new_entry;

    // 사이즈
    cache->total_cached_bytes += size;
}

/**
 * cache_remove_by_entry_unmanaged - entry를 기반으로 해당 캐시 객체를 제거
 * 중요! 반드시 외부에서 락 관리!
 * 
 * @param cache: 캐시 포인터
 * @param entry: 해당 캐시 객체의 포인터
 * @return void
 */
void cache_remove_by_entry_unmanaged(cache_t* cache, cache_entry_t* entry){
    assert(entry != NULL);

    // entry 빠지고, 그 전후를 이어줌, cache의 head 및 tail도 이어줌
    if (entry->prev)
        entry->prev->next = entry->next;
    else
        cache->head = entry->next;
    
    if (entry->next)
        entry->next->prev = entry->prev;
    else
        cache->tail = entry->prev;

    // 해시 테이블 체이닝에서 제거
    int hashed_index = hash_uri(entry->uri);
    cache_entry_t* curr = cache->hashtable[hashed_index];
    cache_entry_t* prev = NULL;
    
    while(curr){ // 체이닝쪽
        if(curr == entry){
            if (prev)
                prev->h_next = curr->h_next;
            else
                cache->hashtable[hashed_index] = curr->h_next;
            break;
        }   
        prev = curr;
        curr = curr->h_next;
    }
    
    cache->total_cached_bytes -= entry->content_length;
    free(entry->content);
    free(entry);
}

/**
 * cache_lookup - uri 기반으로 캐시에서 찾아봄
 * 
 * @param cache: 캐시 포인터
 * @param uri: 요청 URI (key)의 포인터
 * @param internal_lock: internal_lock 1이면 락을 내부에서 관리. 0이면 외부에서 관리.
 * @param update_lru: update_lru 1이면 LRU 업데이트, 0이면 안함. (cache_insert에서도 이걸 쓰는데 거기서 LRU 업데이트를 왜 하겠나?)
 * @return void
 */
cache_entry_t* cache_lookup(cache_t* cache, const char* uri, const int internal_lock, const int update_lru){
    if (internal_lock)
        pthread_rwlock_wrlock(&cache->ptrwlock);

    // 해시값 구함 ==> 해시 테이블 ==> 해시 체이닝 순서.
    cache_entry_t* entry = cache->hashtable[hash_uri(uri)];
    while (entry && strcmp(entry->uri, uri) != 0)  // 해시 체이닝의 끝까지 - entry가 NULL여도 탈출
        entry = entry->h_next;

    // 이중 연결 리스트의 prev/next는 해시 체이닝 리스트와 사실상 별개로 작동.
    if (entry && update_lru)
        move_to_front_unmanaged(cache, entry); 

    if (internal_lock)
        pthread_rwlock_unlock(&cache->ptrwlock);
    return entry;
}

/**
 * cache_remove - uri 기반으로 캐시에서 제거
 * 락을 내부에서 직접 관리!
 * 
 * @param cache: 캐시 포인터
 * @param uri: 요청 URI (key)의 포인터
 */
void cache_remove(cache_t* cache, const char* uri) {
    pthread_rwlock_wrlock(&cache->ptrwlock);

    cache_entry_t* entry = cache->hashtable[hash_uri(uri)];
    while (entry && strcmp(entry->uri, uri) != 0)
        entry = entry->h_next;

    if (entry != NULL)
        cache_remove_by_entry_unmanaged(cache, entry);

    pthread_rwlock_unlock(&cache->ptrwlock);
}

/**
 * cache_evict_policy_unmanaged - 정책 기반으로 퇴출
 * 중요! 얘는 락을 관리하지 않음!
 * 
 * @param cache: 캐시 포인터
 * @param required_size: 지금 넣으려는 크기
 */
void cache_evict_policy_unmanaged(cache_t* cache, int required_size) {
    while (cache->total_cached_bytes + required_size >= MAX_CACHE_SIZE){
        if (cache->tail == NULL) 
            break; // 캐시가 비었는데도 공간이 부족한 경우
        evict_lru_unmanaged(cache);
    }
}

/**
 * cache_size - 캐시 크기
 * 
 * @param cache: 캐시 포인터
 */
inline int cache_size(cache_t* cache){
    return cache->total_cached_bytes;
}

/**
 * debug_print_cache - 디버깅 함수
 * 
 * @param cache: 캐시 포인터
 */
void debug_print_cache(cache_t* cache) {
    pthread_rwlock_rdlock(&cache->ptrwlock);

    printf("====== Cache Current State ======\n");
    printf("Total cached bytes: %zu\n", cache->total_cached_bytes);

    cache_entry_t* curr = cache->head;
    while (curr != NULL) {
        printf("URI: %-60s | Size: %d bytes\n", curr->uri, curr->content_length);
        curr = curr->next;
    }
    printf("========== End of Cache =========\n");

    pthread_rwlock_unlock(&cache->ptrwlock);
}
