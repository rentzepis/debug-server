#include "cache.h"

void cache_init()
{
    cache.block_count = 0;
    cache.capacity = MAX_CACHE_SIZE / MAX_OBJECT_SIZE;
    timestamp = 0;
    readcnt = 0;
    sem_init(&mutex, 0, 1);
    sem_init(&rw_mutex, 0, 1);
}

int find_cache(rio_t *rio, char *url)
{
    // 使用全局变量 readcnt，需要加锁
    P(&mutex);
    readcnt++;
    // 第一个读者需要加读写锁，保证不会有写者同时访问，同时允许其他读者访问
    if (readcnt == 1)
    {
        P(&rw_mutex);
    }
    V(&mutex); // 释放全局变量 readcnt 的访问权
    int flag = 0;
    for (int i = 0; i < cache.capacity; i++)
    {
        if (strcmp(cache.blocks[i].url, url) == 0)
        {
            // 更新时间戳，也是全局变量，需要加锁
            P(&mutex);
            cache.blocks[i].last_used = timestamp++;
            V(&mutex);
            // 直接将缓存内容发送到客户端
            rio_writen(rio->rio_fd, cache.blocks[i].content, cache.blocks[i].content_size);
            flag = 1;
            break;
        }
    }
    // 同上，使用全局变量 readcnt，需要加锁
    P(&mutex);
    readcnt--;
    // 最后一个读者需要解读写锁，允许写者访问
    if (readcnt == 0)
    {
        V(&rw_mutex);
    }
    V(&mutex);
    return flag;
}

void insert_cache(char *url, char *content, int content_size)
{
    // 修改缓存，需要加读写锁
    P(&rw_mutex);
    // 缓存已满，需要替换最近最少使用的缓存块
    if (cache.block_count == cache.capacity)
    {
        int min = 0;
        for (int i = 1; i < cache.capacity; i++)
        {
            if (cache.blocks[i].last_used < cache.blocks[min].last_used)
                min = i;
        }
        strcpy(cache.blocks[min].url, url);
        memcpy(cache.blocks[min].content, content, content_size);
        cache.blocks[min].content_size = content_size;
        P(&mutex);
        cache.blocks[min].last_used = timestamp++; // 更新时间戳
        V(&mutex);
    }
    else // 缓存未满，直接添加
    {
        strcpy(cache.blocks[cache.block_count].url, url);
        memcpy(cache.blocks[cache.block_count].content, content, content_size);
        cache.blocks[cache.block_count].content_size = content_size;
        // 更新时间戳和已使用的缓存块数量
        P(&mutex);
        cache.blocks[cache.block_count].last_used = timestamp++;
        V(&mutex);
        cache.block_count++;
    }
    V(&rw_mutex); // 释放读写锁
}