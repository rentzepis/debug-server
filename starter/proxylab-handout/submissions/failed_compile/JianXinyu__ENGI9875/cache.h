#ifndef __CACHE_H__
#define __CACHE_H__


#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000 // 1MB, 2^20 = 1048576
#define MAX_OBJECT_SIZE 102400 // 100KB
#define CACHE_OBJS_COUNT 15    // the PXYDRIVE test 15 cached files

typedef struct 
{
    int isEmpty;
    int stamp;      // timestamp, the LRU cache block has the smallest timestamp
    int readcnt;    // counts the number of readers currently in the critical section, initially = 0 
    sem_t mutex;    // protects access to the shared readcnt variable, initially = 1
    sem_t w;        // controls access to the critical sections that access the shared object, initially = 1
    char cache_obj[MAX_OBJECT_SIZE];
    char cache_url[MAXLINE];
} cache_block;

// global variable 
cache_block cache[CACHE_OBJS_COUNT];    // cache 
char ret_obj[MAX_OBJECT_SIZE];          // the requested object

/* public cache function */
void cache_init();
const char * cache_find(char *url);
void cache_put(char *url,char *buf);

#endif  /* __CACHE_H__*/
