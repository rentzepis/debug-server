#include <stdio.h>
#include "csapp.h"
#include "cache.h"

void cache_init(void)
{
    cache.currentTime = 0;
    cache.readcnt = 0;
    cache.totalSize = 0;
    Sem_init(&cache.readcnt_mutex, 0 ,1);
    Sem_init(&cache.writer_mutex, 0 ,1);
    int i;
    for(i = 0; i< CACHE_LINE_NUM; i++)
    {
        cache.cache_line[i].valid = 0;
        cache.cache_line[i].timestamp = 0;
        cache.cache_line[i].size = 0;
    }
}

// void reader(void)
// {
//     while(1)
//     {
//         P(&mutex);
//         readcnt++;
//         if(readcnt == 1) /*First in*/
//             P(&w);
//         V(&mutex);

//         /*Critical section*/
//         /*Reading happens*/
//         P(&mutex);
//         readcnt--;
//         if(readcnt == 0) /*Last out*/
//             V(&w);
//         V(&mutex);

//     }
// }

int cache_read(char *uri, int fd)
{
    int i;
    int index = -1;
    for(i = 0; i < CACHE_LINE_NUM; i++)
    {
        P(&cache.readcnt_mutex);
        cache.readcnt++;
        if(cache.readcnt == 1)  /*First in*/
        {
            P(&cache.writer_mutex);
        } 
            
        V(&cache.readcnt_mutex);
        if(cache.cache_line[i].valid)
        {
            /*Critical section*/
            /*Reading happens*/
            if(strcmp(uri, cache.cache_line[i].uri) == 0)
            {
                //printf("cache: %s\n",cache.cache_line[index].cache_object);
                index = i;
                (cache.cache_line[index].timestamp)++;
                Rio_writen(fd, cache.cache_line[index].cache_object, cache.cache_line[index].size);
            }
        }
        P(&cache.readcnt_mutex);
        cache.readcnt--;
        if(cache.readcnt == 0) /*Last out*/
        {
            V(&cache.writer_mutex);
        } 
            
        V(&cache.readcnt_mutex);
    }
    return index;
}

// void writer(void)
// {
//     while(1)
//     {
//          P(&w);
//          /*Critical section*/
//          /*Writing happens*/
//          V(&w);
//     }
// }

void cache_write(char *server_res, char* uri, int size)
{
    if(cache.totalSize + size > MAX_CACHE_SIZE) return;
    if(size > MAX_OBJECT_SIZE) return;
#if 1
    int min_idx = -1;
    int i;
    for(i = 0; i < CACHE_LINE_NUM; i++)
    { 
        if(cache.cache_line[i].valid == 0)
        {
            min_idx = i;
            break;
        }
    }
    if(min_idx == -1)
    {
        int mxTime = 0;
        for(i = 0; i < CACHE_LINE_NUM; i++)
        { 
            if(cache.cache_line[i].valid &&  cache.currentTime - cache.cache_line[i].timestamp > mxTime)
            {
                mxTime = cache.currentTime - cache.cache_line[i].timestamp;
                min_idx = i;
            }
        }        
    }
#else
    int min_idx = 0;
    int i;
    for(i = 1; i < CACHE_LINE_NUM; i++)
    { 
        if(cache.cache_line[i].timestamp <  cache.cache_line[min_idx].timestamp)
        {
            min_idx = i;
        }
    }
#endif
    /*Critical section*/
    /*Writing happens*/    
    P(&cache.writer_mutex);
    printf("write cache\n");
    strcpy(cache.cache_line[min_idx].cache_object, server_res);
    strcpy(cache.cache_line[min_idx].uri, uri);
    cache.cache_line[min_idx].size = size;
    cache.cache_line[min_idx].timestamp = ++cache.currentTime;
    cache.cache_line[min_idx].valid = 1;
    cache.totalSize += size;
    V(&cache.writer_mutex);
}
