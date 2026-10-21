/*
 * Name: Zhang Liheng
 * ID: 2200013214
 *
 * A cache storing content from the web server, identifying objects by URL.
 * It employs the reader-writer model by using rwlock provided by pthread.
 */

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_SIZE 10

/*
 * init_cache - allocate space and initialize locks
 * return 0 on success, -1 on error.
 */
int init_cache(void);

/*
 * search_cache - search cache by URL
 * return object length on success, -1 on failure.
 */
int search_cache(char url[MAXLINE], char buf[MAX_OBJECT_SIZE]);

/*
 * store_cache - store object in cache, with LRU policy.
 */
void store_cache(char url[MAXLINE], char buf[MAX_OBJECT_SIZE], int length);

/*
 * destroy_cache - destroy locks
 */
void cache_destory(void);