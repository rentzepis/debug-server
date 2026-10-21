/*
 * @Descripttion: 
 * @version: 
 * @Author: xp.Zhang
 * @Date: 2024-02-19 06:09:39
 * @LastEditors: xp.Zhang
 * @LastEditTime: 2024-02-19 06:09:51
 */
#include "csapp.h"
#include "cache.h"

void initializeCache(cache_t* cache){
    cache->head = Malloc(sizeof(*(cache->head)));
    cache->head->flag = '@';
    cache->head->prev = NULL;
    cache->head->next = NULL;

    cache->tail = Malloc(sizeof(*(cache->tail)));
    cache->tail->flag = '@';
    cache->tail->prev = NULL;
    cache->tail->next = NULL;

    /* construct the doubly linked list */
    cache->head->next = cache->tail;
    cache->tail->prev = cache->head;

    cache->nitems = 0;
}