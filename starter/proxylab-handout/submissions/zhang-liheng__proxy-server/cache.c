#define _XOPEN_SOURCE 500
/*
 * Name: Zhang Liheng
 * ID: 2200013214
 *
 * A cache storing content from the web server, identifying objects by URL.
 * It employs the reader-writer model by using rwlock provided by pthread.
 */
#include <limits.h>
#include "cache.h"

struct _cache
{
    int timer; // = global_timer every time the object is used
    char url[MAXLINE];
    char content[MAX_OBJECT_SIZE];
    int length;
    pthread_rwlock_t lock;
};

struct _cache *cache = NULL;
int global_timer = 0;
pthread_mutex_t timer_mutex;

/*
 * init_cache - allocate space and initialize locks
 * return 0 on success, -1 on error.
 */
int init_cache(void)
{
    cache = calloc(MAX_CACHE_SIZE, sizeof(*cache));
    if (!cache)
        return -1;

    for (int i = 0; i < MAX_CACHE_SIZE; i++)
        pthread_rwlock_init(&cache[i].lock, NULL);

    pthread_mutex_init(&timer_mutex, NULL);

    return 0;
}

/*
 * search_cache - search cache by URL
 * return object length on success, -1 on failure.
 */
int search_cache(char url[MAXLINE], char buf[MAX_OBJECT_SIZE])
{
    int length = -1;

    for (int i = 0; i < MAX_CACHE_SIZE; i++)
    {
        pthread_rwlock_rdlock(&cache[i].lock);

        if (!strcmp(url, cache[i].url))
        {
            pthread_mutex_lock(&timer_mutex);
            ++global_timer;
            cache[i].timer = global_timer;
            pthread_mutex_unlock(&timer_mutex);

            memcpy(buf, cache[i].content, cache[i].length);
            length = cache[i].length;

            pthread_rwlock_unlock(&cache[i].lock);
            break;
        }

        pthread_rwlock_unlock(&cache[i].lock);
    }

    return length;
}

/*
 * store_cache - store object in cache, with LRU policy.
 */
void store_cache(char url[MAXLINE], char buf[MAX_OBJECT_SIZE], int length)
{
    int dest = -1, min_timer = INT_MAX;

    for (int i = 0; i < MAX_CACHE_SIZE; i++)
    {
        pthread_rwlock_rdlock(&cache[i].lock);

        if (!cache[i].timer) // found an empty slot
        {
            dest = i;
            pthread_rwlock_unlock(&cache[i].lock);
            break;
        }
        else if (cache[i].timer < min_timer) // search for LRU object
        {
            dest = i, min_timer = cache[i].timer;
        }

        pthread_rwlock_unlock(&cache[i].lock);
    }

    pthread_mutex_lock(&timer_mutex);
    ++global_timer;

    pthread_rwlock_wrlock(&cache[dest].lock);
    cache[dest].timer = global_timer;

    pthread_mutex_unlock(&timer_mutex);

    strcpy(cache[dest].url, url);
    memcpy(cache[dest].content, buf, length);
    cache[dest].length = length;
    pthread_rwlock_unlock(&cache[dest].lock);
}

/*
 * destroy_cache - destroy locks
 */
void destroy_cache(void)
{
    for (int i = 0; i < MAX_CACHE_SIZE; i++)
        pthread_rwlock_destroy(&cache[i].lock);
    pthread_mutex_destroy(&timer_mutex);
}