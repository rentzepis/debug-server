#include <semaphore.h>
#include "cache.h"

// 캐시 리스트 초기화
cache_header *cache_init()
{
    cache_header *ch;
    ch = (cache_header *)Malloc(sizeof(cache_header));

    ch->head = NULL;
    ch->readcnt = 0;
    ch->current_cache_size = 0;
    Sem_init(&ch->w, 0, 1);
    Sem_init(&ch->mutex, 0, 1);

    return ch;
}

// 캐시 리스트 가장 앞에 캐시 data 추가
int cache_append(cache_header *ch, char *url, char *response_header, char *response_body, int filesize)
{
    // body 크기가 MAX_OBJECT_SIZE보다 크면 return
    if (filesize > MAX_OBJECT_SIZE)
    {
        printf("=====Response Body is bigger than MAX_OBJECT_SIZE=====\r\n\r\n");
        return 0;
    }

    cache_t *new_cache;
    new_cache = (cache_t *)Malloc(sizeof(cache_t));

    strcpy(new_cache->url, url);
    strcpy(new_cache->response_header, response_header);
    strcpy(new_cache->response_body, response_body);
    new_cache->filesize = filesize;
    new_cache->prev = NULL;
    new_cache->next = NULL;

    // 캐시 리스트 읽는 연산 -> reader lock
    // 현재 캐시 리스트에 빈공간이 부족할 경우 LRU data를 삭제해서 공간 확보
    P(&ch->mutex);
    ch->readcnt++;
    if (ch->readcnt == 1)
        P(&ch->w);
    V(&ch->mutex);

    while (ch->current_cache_size + filesize > MAX_CACHE_SIZE)
        cache_delete_LRU(ch);

    P(&ch->mutex);
    ch->readcnt--;
    if (ch->readcnt == 0)
        V(&ch->w);
    V(&ch->mutex);

    // 캐시 리스트 쓰는 연산 -> writer lock
    // 캐시 리스트 가장 앞에 data 추가
    P(&ch->w);
    if (ch->head != NULL)
    {
        ch->head->prev = new_cache;
        new_cache->next = ch->head;
    }

    ch->head = new_cache;
    ch->current_cache_size += filesize;
    V(&ch->w);

    return 1;
}

// 캐시 리스트의 마지막 data 삭제
void cache_delete_LRU(cache_header *ch)
{
    cache_t *lru_cache = ch->head;
    cache_t *prev_cache = NULL;

    // 캐시 리스트 읽는 연산 -> reader lock
    // LRU data = 마지막 data 검색
    P(&ch->mutex);
    ch->readcnt++;
    if (ch->readcnt == 1)
        P(&ch->w);
    V(&ch->mutex);

    if (lru_cache == NULL)
        return;

    while (lru_cache->next != NULL)
    {
        prev_cache = lru_cache;
        lru_cache = lru_cache->next;
    }

    P(&ch->mutex);
    ch->readcnt--;
    if (ch->readcnt == 0)
        V(&ch->w);
    V(&ch->mutex);

    // 캐시 리스트 쓰는 연산 -> writer lock
    // 검색한 data 삭제
    P(&ch->w);
    if (prev_cache == NULL)
        ch->head = NULL;
    else
        prev_cache->next = NULL;

    ch->current_cache_size -= lru_cache->filesize;

    Free(lru_cache);
    V(&ch->w);
}

cache_t *cache_search(cache_header *ch, char *url)
{
    cache_t *cur_cache = ch->head;
    cache_t *temp;

    // 여기에 reader lock을 해줘야 하는데 오류남
    // 오류 수정하지 못함
    while (cur_cache != NULL)
    {
        if (!strcmp(cur_cache->url, url))
            break;

        cur_cache = cur_cache->next;
    }

    if (cur_cache == NULL)
        return NULL;

    // 캐시 리스트 쓰는 연산 -> writer lock
    // 최근에 참조된 캐시 data를 리스트 가장 앞으로 이동
    P(&ch->w);
    cache_move_to_head(ch, cur_cache);
    V(&ch->w);

    return cur_cache;
}

void cache_move_to_head(cache_header *ch, cache_t *cache)
{
    if (cache->prev != NULL)
    {
        if (cache->next != NULL)
        {
            cache->prev->next = cache->next;
            cache->next->prev = cache->prev;
        }
        else
        {
            cache->prev->next = NULL;
        }

        ch->head->prev = cache;
        cache->next = ch->head;
        ch->head = cache;
    }
}

// 캐시 data 하나 출력
void cache_display(cache_t *cache)
{
    if (cache == NULL)
    {
        printf("Cache is NULL\r\n\r\n");
        return;
    }

    printf("URL : %s\r\n", cache->url);
    printf("filesize : %d\r\n", cache->filesize);
    printf("Next : %p\r\n", cache->next);
    printf("Prev : %p\r\n\r\n", cache->prev);
}

// 캐시 리스트 출력
void cache_all_display(cache_header *ch)
{
    cache_t *cur_cache = ch->head;

    while (cur_cache != NULL)
    {
        cache_display(cur_cache);
        cur_cache = cur_cache->next;
    }

    printf("Total Cache Size : %d / %d\r\n\r\n", ch->current_cache_size, MAX_CACHE_SIZE);
}