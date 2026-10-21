#ifndef CACHE_H
#define CACHE_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// 캐시 노드를 정의하는 구조체
typedef struct cache_node {
    char *key;  // URI와 같은 캐시 키
    char *value; // 요청에 대한 응답 (HTML 데이터 등)
    size_t size; // 데이터의 크기
    struct cache_node *prev; // 이전 캐시 항목
    struct cache_node *next; // 다음 캐시 항목
} cache_node;

// LRU 제거
void evict_cache();

// 캐시에서 항목을 가져오는 함수 선언
const char *cache_get(const char *key);

// 캐시에 항목을 추가하는 함수 선언
void cache_put(const char *key, const char *value, size_t size);

#endif 
