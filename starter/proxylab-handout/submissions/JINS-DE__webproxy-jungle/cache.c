#include "cache.h"

#define MAX_CACHE_SIZE 1049000   // 최대 캐시 크기
#define MAX_OBJECT_SIZE 102400   // 개별 객체의 최대 크기

static cache_node *head = NULL;  // 가장 최근에 사용된 항목
static cache_node *tail = NULL;  // 가장 오래된 항목(LRU)
static size_t current_cache_size = 0;  // 현재 캐시의 총 크기

// 캐시 크기가 초과할 때 tail 노드를 제거하는 함수(LRU 제거)
void evict_cache() {

    while (current_cache_size > MAX_CACHE_SIZE) {
        if (tail == NULL) return; // 캐시가 비어 있으면 함수 종료

        // 가장 오래된 항목(LRU, tail)을 제거
        cache_node *old_tail = tail;
        current_cache_size -= old_tail->size; // 캐시 크기에서 해당 항목의 크기만큼 감소

        if (old_tail->prev) {
            tail = old_tail->prev;
            tail->next = NULL;
        } else {
            // 캐시에 하나의 항목만 있을 경우
            head = NULL;
            tail = NULL;
        }

        // 제거된 노드의 메모리 해제
        free(old_tail->key);
        free(old_tail->value);
        free(old_tail);
    }
}

// 새로운 객체를 캐시에 추가하는 함수
void cache_put(const char *key, const char *value, size_t size) {
    // 코드의 재사용성과 견고성을 위해 `size > MAX_OBJECT_SIZE` 조건을 한 번 더 넣어줌
    if (size > MAX_OBJECT_SIZE) {
        return; // 객체의 크기가 MAX_OBJECT_SIZE보다 크면 캐시하지 않음
    }

    // 이미 캐시에 동일한 객체가 있는지 확인
    cache_node *node = head;
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            // 기존 객체가 있으면 값 업데이트 후, 가장 최근에 사용된 항목으로 이동
            free(node->value); // 기존 값 해제
            node->value = strdup(value); // 새로운 값으로 갱신
            node->size = size;

            // 노드를 가장 최근 항목(리스트의 head)으로 이동
            if (node != head) {
                if (node->prev) {
                    node->prev->next = node->next;
                }
                if (node->next) {
                    node->next->prev = node->prev;
                }
                if (node == tail) {
                    tail = node->prev;
                }

                node->next = head;
                node->prev = NULL;
                if (head) {
                    head->prev = node;
                }
                head = node;

                if (tail == NULL) {
                    tail = head;
                }
            }
            return;
        }
        node = node->next;
    }

    // 새로운 캐시 항목 생성
    cache_node *new_node = malloc(sizeof(cache_node));
    new_node->key = strdup(key); // 키 복사
    new_node->value = strdup(value); // 값 복사
    new_node->size = size;
    new_node->prev = NULL;
    new_node->next = head;

    // 새로운 노드를 리스트의 앞(head)로 추가
    if (head != NULL) {
        head->prev = new_node;
    }
    head = new_node;

    if (tail == NULL) {
        tail = head;
    }

    current_cache_size += size; // 캐시 크기에 새 항목 크기 추가

    // 캐시 크기가 초과하면 오래된 항목(LRU)을 제거
    evict_cache();
}

// 캐시에서 항목을 검색하는 함수
const char *cache_get(const char *key) {
    cache_node *node = head;

    // 캐시 리스트를 순회하며 주어진 키에 해당하는 항목을 검색
    while (node != NULL) {
        if (strcmp(node->key, key) == 0) {
            // 해당 항목을 찾으면 가장 최근 항목으로 이동
            if (node != head) {
                if (node->prev) {
                    node->prev->next = node->next;
                }
                if (node->next) {
                    node->next->prev = node->prev;
                }
                if (node == tail) {
                    tail = node->prev;
                }

                node->next = head;
                node->prev = NULL;
                if (head != NULL) {
                    head->prev = node;
                }
                head = node;

                if (tail == NULL) {
                    tail = head;
                }
            }
            return node->value; // 캐시된 값 반환
        }
        node = node->next;
    }

    return NULL; // 캐시 미스 (해당 항목이 없을 경우)
}
