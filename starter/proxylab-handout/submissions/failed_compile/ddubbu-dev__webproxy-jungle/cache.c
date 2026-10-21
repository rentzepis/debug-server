#include "csapp.h"
#include <stdio.h>
#include <stdlib.h>

#include "cache.h"

DLL *createDoublyLinkedList() {
    DLL *new_dll = (DLL *)calloc(1, sizeof(DLL));
    CacheNode *head_node = (CacheNode *)calloc(1, sizeof(CacheNode));

    new_dll->head = head_node;
    head_node->next = head_node->prev = head_node;
    new_dll->sum_of_cache_object_size = 0;
    return new_dll;
}

CacheNode *createNode(RequestInfo req, ResponseInfo res) {
    CacheNode *node = (CacheNode *)calloc(1, sizeof(CacheNode));

    // request 복사
    node->req = req; // 구조체 값 복사

    // response 복사
    node->res.body = (char *)malloc(res.body_size);
    memcpy(node->res.body, res.body, res.body_size);

    node->res.body_size = res.body_size;

    node->prev = NULL;
    node->next = NULL;

    return node;
}

// [LRU] 앞에 최신정보 넣기
void pushFront(DLL *dll, RequestInfo req, ResponseInfo res) {
    CacheNode *new_node = createNode(req, res);

    new_node->next = dll->head->next;
    dll->head->next->prev = new_node;
    dll->head->next = new_node;
    new_node->prev = dll->head;

    dll->sum_of_cache_object_size += res.body_size; // the actual web objects (요청 응답 bytes 수만 카운트함)
    printf("[push front] method=%s, path=%s\n", new_node->req.method, new_node->req.path);
    printDll(dll);

    if (dll->sum_of_cache_object_size > MAX_CACHE_SIZE) {
        popBack(dll);
    }
}

// [LRU] 뒤에서 과거정보 제거
void popBack(DLL *dll) {
    if (dll->sum_of_cache_object_size == 0) {
        printf("List is empty, cannot pop.\n");
        return;
    }

    CacheNode *pop_node = dll->head->prev;

    dll->head->prev = pop_node->prev;
    pop_node->prev->next = dll->head;
    printf("[pop back] method=%s, path=%s\n", pop_node->req.method, pop_node->req.path);
    printDll(dll);

    dll->sum_of_cache_object_size -= pop_node->res.body_size;

    free(pop_node->res.body);
    free(pop_node);
}

CacheNode *search(DLL *dll, RequestInfo req_info) {
    if (dll == NULL || dll->head == NULL) {
        printf("DLL or head is NULL\n");
        return NULL;
    }

    CacheNode *find_node = dll->head->next;

    while (find_node != dll->head) {
        if (!strcmp(find_node->req.method, req_info.method) && !strcmp(find_node->req.path, req_info.path)) {
            printf("[search][yes] method=%s, path=%s\n", find_node->req.method, find_node->req.path);
            return find_node;
        }
        find_node = find_node->next;
    }

    printf("[search][no] method=%s, path=%s\n", req_info.method, req_info.path);
    return NULL;
}

void moveFront(DLL *dll, CacheNode *move_node) {
    move_node->prev->next = move_node->next;
    move_node->next->prev = move_node->prev;

    move_node->next = dll->head->next;
    dll->head->next->prev = move_node;
    dll->head->next = move_node;
    move_node->prev = dll->head;
    printDll(dll);
}

void findAndMoveFront(DLL *dll, RequestInfo req) {
    CacheNode *move_node = search(dll, req);
    if (!move_node)
        return;

    move_node->prev->next = move_node->next;
    move_node->next->prev = move_node->prev;

    move_node->next = dll->head->next;
    dll->head->next->prev = move_node;
    dll->head->next = move_node;
    move_node->prev = dll->head;
    printDll(dll);
}

void printDll(DLL *dll) {
    printf("[앞] ");
    if (!dll->head->next) {
        printf("[]\n");
        return;
    }

    CacheNode *now = dll->head->next;
    while (now != dll->head->prev) {
        printf("(%s, %s) ←→ ", now->req.method, now->req.path);
        now = now->next;
    }
    printf("(%s, %s) ←→ \n\n", now->req.method, now->req.path);
}

void deleteList(DLL *dll) {
    CacheNode *delete_node = dll->head->next;
    while (delete_node != dll->head) {
        CacheNode *next_node = delete_node->next;
        free(delete_node->res.body);
        free(delete_node);
        delete_node = next_node;
    }
    free(dll->head);
    free(dll);
}
