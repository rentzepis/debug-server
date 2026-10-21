/*
 * name: 赵媛媛
 * student-id: <2300013077>
 */
#ifndef __CACHE_H__
#define __CACHE_H__
#include <stdio.h>
#include "csapp.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_NUM (MAX_CACHE_SIZE / MAX_OBJECT_SIZE)

typedef struct {
    char url[MAXLINE];
    char data[MAX_OBJECT_SIZE];
    int data_size;
    int vis_time;
} Cacheline;

void init_cache();

// check if exists in cache, if does, forward to the client & update vis_time
int exist_cache(char url_request[MAXLINE], int connfd); 

// add to the cache, if is full, evict LRU
void do_cache(char url_request[MAXLINE], char data_buf[MAX_OBJECT_SIZE], int total_size); 

#endif 