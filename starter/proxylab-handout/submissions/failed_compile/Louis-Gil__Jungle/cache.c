#include "cache.h"

CacheNode* pop(CacheList *list);
void push(CacheList* list,CacheNode* node);
void delete(CacheList* list,CacheNode* node);

CacheList * initCache(){
    CacheList* return_p = NULL;
    return_p = (CacheList*)Malloc(sizeof (CacheList));
    return_p->frontNode = NULL;
    return_p->rearNode = NULL;
    return_p->currentElementCount = 0;
    return return_p;
}

char* findCacheNode(CacheList* list,char* url){
    printf("here is findCacheNode()\n");
    if (list->currentElementCount == 0) {
        printf("first case\n");
        return NULL;
    }
    CacheNode* currentNode = list->frontNode;
    for (int i = 0; i < list->currentElementCount; ++i) {
        //cache hit
        if (strcmp(currentNode->url, url) == 0) {
            if (currentNode == list->rearNode) {
                push(list, pop(list));
            }else if(currentNode != list->frontNode){
                delete(list, currentNode);//중간 노드 삭제
                push(list, currentNode);
            }
            return currentNode->data;
        }
        currentNode = currentNode->next;
    }
    return NULL;//cache miss
}

void insertCacheNode(CacheList *list, char *url,char* response) {
    printf("=======insertCacheNode Start=======\n");
    CacheNode *newNode = NULL;
    if (list != NULL) {
        newNode = (CacheNode *) Malloc(sizeof(CacheNode));
        strcpy(newNode->url,url);
        strcpy(newNode->data,response);
        /* 캐시가 가득 찬 경우 */
        if(list->currentElementCount == MAX_CACHE_COUNT) {
            Free(pop(list));
        }
        push(list, newNode);
    }
    printf("=======insertCacheNode end=======\n");
}

CacheNode* pop(CacheList *list) {
    CacheNode *targetNode = list->rearNode;
    targetNode->prev->next = NULL;
    list->rearNode = targetNode->prev;
    list->currentElementCount--;
    return targetNode;
}

void push(CacheList* list,CacheNode* node){
    printf("=======push Start=======\n");
    if (list->currentElementCount == 0) {
        list->frontNode = node;
        node->prev = NULL;
        list->currentElementCount++;
        return;
    }
    list->frontNode->prev = node;
    node->next = list->frontNode;
    node->prev = NULL;
    list->frontNode = node;
    list->currentElementCount++;
    printf("=======push End=======\n");
}

void delete(CacheList* list,CacheNode* node){
    node->prev->next = node->next;
    node->next->prev = node->prev;
    list->currentElementCount--;
}

void deleteCache(CacheList *list){
    if (list->currentElementCount != 0) {
        CacheNode *target = list->frontNode;
        CacheNode *temp = NULL;
        while (target->next != NULL) {
            temp = target;
            Free(target);
            target = temp->next;
        }
        Free(list);
    }
}