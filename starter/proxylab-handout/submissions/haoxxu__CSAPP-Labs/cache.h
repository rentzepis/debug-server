/* 
 * cache.h - prototypes and definitions of cache for proxy lab
 */

/* $begin cahce.h */
#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1107000
#define MAX_OBJECT_SIZE 102400
#define MAX_OBJECT_CNT 10

/* Use uri to distinguish different requests instead of url */
/* Some url may have "http://", some may not, and they are actually the same requests */
typedef struct {
    int valid;
    int last_used_time;
    int readcnt;         /* Initially = 0 */
    sem_t mutex_access;  /* mutex for the access to the cache */
    sem_t mutex_readcnt; /* mutex for readcnt */

    char uri[MAXLINE];
    char content[MAX_OBJECT_SIZE];
} cache_line_t;

typedef struct {
    int time;
    sem_t mutex_time; /* mutex for time */
    cache_line_t cache_lines[MAX_OBJECT_CNT];
} cache_t;

/* Wrapper functions of P/V as reader/writer */
void P_reader(cache_t *cachep, int index);
void V_reader(cache_t *cachep, int index);
void P_writer(cache_t *cachep, int index);
void V_writer(cache_t *cachep, int index);

/* Implementation function of cache */
void cache_init(cache_t *cachep);
int cache_find(cache_t *cachep, char *uri);
void cache_insert(cache_t *cachep, char *uri, char *content);

#endif /* __CACHE_H__ */
/* $end cache.h */