#include "cache.h"

/* P_reader
 *  - Wrapper function of P as a reader.
 *  - P mutex_access if it's the first one read.
 */
void P_reader(cache_t *cachep, int index) {
    P(&(cachep->cache_lines[index].mutex_readcnt));
    ++(cachep->cache_lines[index].readcnt);
    if (cachep->cache_lines[index].readcnt == 1)
        P(&(cachep->cache_lines[index].mutex_access));
    V(&(cachep->cache_lines[index].mutex_readcnt));
}

/* V_reader
 *  - Wrapper function of V as a reader.
 *  - V mutex_access if it's the last one read.
 */
void V_reader(cache_t *cachep, int index) {
    P(&(cachep->cache_lines[index].mutex_readcnt));
    --(cachep->cache_lines[index].readcnt);
    if (cachep->cache_lines[index].readcnt == 0)
        V(&(cachep->cache_lines[index].mutex_access));
    V(&(cachep->cache_lines[index].mutex_readcnt));
}

/* P_writer
 *  - Wrapper function of P as a writer.
 */
void P_writer(cache_t *cachep, int index) {
    P(&(cachep->cache_lines[index].mutex_access));
}

/* V_writer
 *  - Wrapper function of V as a writer.
 */
void V_writer(cache_t *cachep, int index) {
    V(&(cachep->cache_lines[index].mutex_access));
}

/* 
 * cache_init - Initializes the cache
 */
void cache_init(cache_t *cachep) {
    cachep->time = 0;
    for (int i = 0; i < MAX_OBJECT_CNT; ++i) {
        cachep->cache_lines[i].valid = 0;
        cachep->cache_lines[i].last_used_time = 0;
        cachep->cache_lines[i].readcnt = 0;
        Sem_init(&(cachep->cache_lines[i].mutex_access), 0, 1);
        Sem_init(&(cachep->cache_lines[i].mutex_readcnt), 0, 1);
        Sem_init(&(cachep->mutex_time), 0, 1);
    }
}

/* 
 * cache_find
 *  - Find the cache line according to the uri.
 *  - Return the cache line id if found, -1 if not found.
 */
int cache_find(cache_t *cachep, char *uri) {
    P(&(cachep->mutex_time));
    ++cachep->time;
    V(&(cachep->mutex_time));

    for (int i = 0; i < MAX_OBJECT_CNT; ++i) {
        P_reader(cachep, i);

        if (cachep->cache_lines[i].valid && !strcmp(cachep->cache_lines[i].uri, uri)) {
            V_reader(cachep, i);
            
            /* Update last used time */
            P_writer(cachep, i);
            P(&(cachep->mutex_time));
            cachep->cache_lines[i].last_used_time = cachep->time;
            V(&(cachep->mutex_time));
            V_writer(cachep, i);

            return i;
        }

        V_reader(cachep, i);
    }

    return -1;
}

/* 
 * cache_insert
 *  - Insert the content into the cache.
 */
void cache_insert(cache_t *cachep, char *uri, char *content) {
    if (strlen(uri) > MAXLINE || strlen(content) > MAX_OBJECT_SIZE)
        return;
    
    /* Find the cache line to be evicted */
    int evict_id = -1, min_last_used_time = 0x7fffffff;
    for (int i = 0; i < MAX_OBJECT_CNT; ++i) {
        P_reader(cachep, i);
        
        /* Find the empty cache line */
        if (cachep->cache_lines[i].valid == 0) {
            evict_id = i;
            V_reader(cachep, i);
            break; 
        }

        /* Update the index with minimum lastusedtime */
        if (cachep->cache_lines[i].last_used_time < min_last_used_time) {
            min_last_used_time = cachep->cache_lines[i].last_used_time;
            evict_id = i;
        }

        V_reader(cachep, i);
    }

    /* Write data into the cache line to be evicted */
    P_writer(cachep, evict_id);

    cachep->cache_lines[evict_id].valid = 1;
    P(&(cachep->mutex_time));
    cachep->cache_lines[evict_id].last_used_time = cachep->time;
    V(&(cachep->mutex_time));
    cachep->cache_lines[evict_id].readcnt = 0;
    strcpy(cachep->cache_lines[evict_id].uri, uri);
    strcpy(cachep->cache_lines[evict_id].content, content);

    V_writer(cachep, evict_id);
}