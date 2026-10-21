/*
 * name: 赵媛媛
 * student-id: <2300013077>
 */
#include <stdio.h>
#include "csapp.h"
#include "cache.h"

Cacheline cache[MAX_NUM];
sem_t mutex, w;
int readcnt;
int cur_time;
int used_line_num;

void init_cache()
{
    sem_init(&mutex, 0, 1);
    sem_init(&w, 0, 1);
    readcnt = 0;
    cur_time = 0;
    used_line_num = 0;
}

// check if exists in cache, if does, forward to the client & update vis_time
int exist_cache(char url_request[MAXLINE], int connfd)
{
    int ret = 0;
    P(&mutex);
    readcnt ++;
    if (readcnt == 1) 
        P(&w);
    V(&mutex);
    for (int i = 0; i < MAX_NUM; i++) {
        if (!strcmp(cache[i].url, url_request)) {
            ret = 1;
            rio_writen(connfd, cache[i].data, cache[i].data_size);
            P(&mutex);
            cache[i].vis_time = cur_time;
            V(&mutex);
            P(&mutex);
            cur_time ++;
            V(&mutex);
            break;
        }
    }
    P(&mutex);
    readcnt --;
    if (readcnt == 0) 
        V(&w);
    V(&mutex);
    return ret;
}

void do_cache(char url_request[MAXLINE], char data_buf[MAX_OBJECT_SIZE], int total_size)
{
    P(&w);
    int insert_index;
    if (used_line_num == MAX_NUM) { // eviction
        int last_recent_time = cur_time;
        int last_recent_index = 0;
        for (int i = 0; i < MAX_NUM; i++) {
            if (cache[i].vis_time < last_recent_time) {
                last_recent_time = cache[i].vis_time;
                last_recent_index = i;
            }
        }
        insert_index = last_recent_index;
    } else { // directly insert
        insert_index = used_line_num;
        P(&mutex);
        used_line_num ++;
        V(&mutex);
    }
    strcpy(cache[insert_index].url, url_request);
        memcpy(cache[insert_index].data, data_buf, total_size);
        cache[insert_index].vis_time = cur_time;
        cache[insert_index].data_size = total_size; 
        P(&mutex);
        cur_time ++;
        V(&mutex);
    V(&w);
}