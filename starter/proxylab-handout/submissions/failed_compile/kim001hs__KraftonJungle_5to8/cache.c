#include "csapp.h"

/* Recommended max cache and object sizes (권장되는 최대 캐시 크기 및 객체 크기) */
#define MAX_CACHE_SIZE 1049000 // 전체 캐시의 최대 크기(바이트)이다.
#define MAX_OBJECT_SIZE 102400 // 캐시 가능한 단일 객체의 최대 크기(바이트)이다.

/* method type (메서드 타입) */
typedef enum { GET, HEAD } method_t; // 지원하는 HTTP 메서드 타입이다.

/* for cache list (캐시 리스트를 위한 구조체) */
typedef struct _cache_t {
    uint32_t ip; // 서버의 숫자 IP 주소이다.
    uint16_t port; // 서버의 숫자 포트 번호이다.
    method_t method; // 요청 메서드이다.
    char path[MAXLINE]; // 요청 경로이다.
    ssize_t reslen; // 응답 데이터의 길이이다.
    char *response; // 응답 데이터 본문을 가리키는 포인터이다.
    struct _cache_t *prev; // 이전 노드를 가리키는 포인터이다. (LRU를 위한 이중 연결 리스트)
    struct _cache_t *next; // 다음 노드를 가리키는 포인터이다.
} cache_t;

/* global variable for cache list (캐시 리스트를 위한 전역 변수) */
static cache_t *head, *tail; // 이중 연결 리스트의 헤드(가장 최근)와 테일(가장 오래됨) 포인터이다.
static ssize_t *cache_left; // 현재 남은 캐시 공간의 크기(바이트)를 가리키는 포인터이다.

/* cache functions (캐시 관련 함수 선언) */
cache_t *read_cache(u_int32_t, u_int16_t, method_t, char *); // 캐시에서 응답을 읽어온다.
void write_cache(u_int32_t, u_int16_t, method_t, char *, char *, ssize_t *); // 캐시에 응답을 기록한다.
void pop_cache(cache_t *); // 캐시 블록을 리스트에서 제거한다.
void push_cache(cache_t *); // 캐시 블록을 리스트의 맨 앞(가장 최근)으로 이동시킨다.

/** Write cache and push into top (캐시 기록 및 맨 앞으로 이동)
 * @param ip              숫자 IP 주소이다.
 * @param port            숫자 포트 번호이다.
 * @param method          요청 메서드이다.
 * @param path            URI의 경로 문자열이다.
 * @param cache_data      응답 데이터 포인터이다. (MAX_OBJECT_SIZE로 할당되었을 수 있다.)
 * @param cache_len       전체 응답 길이 포인터이다.
 * @return                void
 */
void write_cache(u_int32_t ip, u_int16_t port, method_t method, char *path,
                 char *cache_data, ssize_t *res_len) {
    cache_t *meta; /* 캐시 블록 구조체이다. */
    cache_t *lru;  /* 가장 오랫동안 사용되지 않은 캐시 블록이다. */
    ssize_t length = *res_len; // 응답의 실제 길이이다.
    free(res_len); // 동적으로 할당된 길이 포인터는 해제한다.

    printf("* Write cache.. %d\n", *cache_left);
    
    /* if cache memory is full (캐시 메모리가 가득 찬 경우 LRU 정책 적용) */
    lru = tail->prev; // 가장 오랫동안 사용되지 않은 노드(tail 바로 앞)부터 시작한다.
    
    // 새 응답을 저장할 공간이 부족하거나, 가장 오래된 노드까지 도달할 때까지 반복한다.
    while (length >= (*cache_left) && lru != head) {
        printf("\tCache is full. cleaning..%d %s\n", lru->ip, lru->path);

        /* delete the least recently used and check (가장 오래된 블록 삭제 및 공간 확보) */
        pop_cache(lru); // 리스트에서 해당 블록을 제거한다.
        free(lru->response); // 응답 데이터를 저장했던 메모리를 해제한다.
        (*cache_left) += lru->reslen; // 해제된 공간만큼 남은 캐시 공간을 늘린다.
        lru = lru->prev; // 다음으로 오래된 노드로 이동한다. (lru는 현재 제거된 노드를 가리키므로 prev로 이동)
        
        // 이중 연결 리스트에서 노드가 제거되면 lru 포인터는 제거된 노드를 가리키고, 
        // lru->prev는 그 다음 오래된 노드를 가리키게 된다.
        
        if (lru == head) {
            printf("\tCache overflow\n");
            return; // 캐시를 비웠지만 여전히 공간이 부족하면 저장하지 않고 종료한다.
        }
        free(lru->next); // 리스트에서 제거된 노드(이전 lru) 자체를 해제한다.
    }

    /* make metadata (메타데이터 생성) */
    meta = (cache_t *)malloc(sizeof(cache_t)); // 새 캐시 블록을 할당한다.
    meta->ip = ip;
    meta->port = port;
    meta->method = method;
    strcpy(meta->path, path);

    /* save data (데이터 저장) */
    char *real_size = (char *)malloc(length); // 실제 응답 크기(length)만큼 메모리를 할당한다.
    
    /* reallocate with real size (not MAX_OBJ_SIZE) (실제 크기로 재할당) */
    memcpy(real_size, cache_data, length); // 임시 버퍼(cache_data)에서 실제 메모리(real_size)로 데이터를 복사한다.
    free(cache_data); // 임시 버퍼를 해제한다. (doit 함수에서 할당된 임시 버퍼이다.)
    
    meta->reslen = length; // 실제 응답 길이를 저장한다.
    meta->response = real_size; // 실제 응답 데이터 포인터를 저장한다.
    
    (*cache_left) -= length; // 남은 캐시 공간을 줄인다.
    printf("* Write cache.. fin %d %d\n", length, *cache_left);

    /* push (맨 앞으로 추가) */
    push_cache(meta); // 새 캐시 블록을 리스트의 맨 앞(가장 최근)에 추가한다.
}

/** Read cached response from list (리스트에서 캐시된 응답 읽기)
 * @param ip              숫자 IP를 기준으로 캐시를 검색한다.
 * @param port            숫자 포트 번호를 기준으로 캐시를 검색한다.
 * @param method          요청 메서드를 기준으로 캐시를 검색한다.
 * @param path            경로를 기준으로 캐시를 검색한다.
 * @return                찾으면 cache_t* 포인터를, 못 찾으면 NULL을 반환한다.
 */
cache_t *read_cache(u_int32_t ip, u_int16_t port, method_t method, char *path) {
    cache_t *curr;
    int tmp; // 사용되지 않는 임시 변수이다.

    curr = head->next; // 헤드 다음 노드부터 검색을 시작한다. (헤드와 테일은 더미 노드이다.)
    while (curr != tail) {
        printf("* Search Cache: %d %d %s\n", curr->ip, curr->port, curr->path);
        
        /* if same request (동일한 요청인 경우) */
        // IP, 포트, 메서드, 경로가 모두 일치하는지 확인한다.
        if (curr->ip == ip && curr->port == port && curr->method == method &&
            !strcmp(curr->path, path)) {
            printf("* Cache found %s\n", curr->path);
            pop_cache(curr); /* delete from list (리스트에서 제거한다.) */
            return curr; // 찾은 캐시 블록을 반환한다. (doit에서 다시 push_cache를 호출할 것이다.)
        }
        curr = curr->next; // 다음 노드로 이동한다.
    }
    return NULL; // 캐시를 찾지 못했으면 NULL을 반환한다.
}

/** Remove cache block from list (리스트에서 캐시 블록 제거)
 * @param curr            제거할 캐시 블록 포인터이다.
 * @return                void
 */
void pop_cache(cache_t *curr) {
    // 현재 노드(curr)를 건너뛰고 이전 노드와 다음 노드를 연결한다.
    curr->prev->next = curr->next;
    curr->next->prev = curr->prev;
}

/** Push cache block into list (캐시 블록을 리스트 맨 앞으로 이동)
 * @param curr            맨 앞으로 이동시킬 캐시 블록 포인터이다.
 * @return                void
 */
void push_cache(cache_t *curr) {
    // LRU 정책에 따라 캐시 블록을 리스트의 맨 앞(head 바로 다음)에 삽입한다.
    curr->next = head->next; // 현재 블록의 다음을 이전의 첫 번째 블록으로 설정한다.
    head->next->prev = curr; // 이전의 첫 번째 블록의 이전을 현재 블록으로 설정한다.
    curr->prev = head; // 현재 블록의 이전을 헤드 더미 노드로 설정한다.
    head->next = curr; // 헤드의 다음을 현재 블록으로 설정한다.
}