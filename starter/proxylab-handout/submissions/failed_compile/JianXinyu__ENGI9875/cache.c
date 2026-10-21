#include "csapp.h"
#include "cache.h"

/* private cache functions */
int cache_eviction();
void cache_update(int idx);

void cache_init()
{
   for(int i = 0; i < CACHE_OBJS_COUNT; ++i){
        cache[i].isEmpty = 1;
        cache[i].stamp   = -1;
        cache[i].readcnt = 0;
        Sem_init(&(cache[i].mutex),0,1);
        Sem_init(&(cache[i].w),0,1);
   }
}

/** 
 * find the requested URL in the cache
 * @param url the desired object URL
 * @return if cached, return index in the cache array
 *         if not, return -1
 **/
const char * cache_find(char *url)
{
    ret_obj[0] = '\0';
    for(int i = 0; i < CACHE_OBJS_COUNT; ++i)
    {
        P(&(cache[i].mutex));
        cache[i].readcnt++;
        if(cache[i].readcnt == 1) /* First in */
            P(&cache[i].w);
        V(&cache[i].mutex);

        // if find the desired object
        if( strcmp(url, cache[i].cache_url) == 0 && cache[i].isEmpty == 0 ){
            strcpy(ret_obj, cache[i].cache_obj);
        }

        P(&cache[i].mutex);
        cache[i].readcnt--;
        if(cache[i].readcnt == 0) /* Last out */
            V(&cache[i].w); 
        V(&cache[i].mutex);

        // if find the desired object
        if(ret_obj[0]){
            // update LRU 
            cache_update(i);
            return ret_obj;
        } 
    }
    // if not find
    return NULL;
}

/**
 * choose a victim cache block
 * @return index of victim block with maximal timestamp
 **/ 
int cache_eviction()
{
    int max_stamp = -1, ret = 0;

    for(int i = 0; i < CACHE_OBJS_COUNT; ++i ){
        
        P(&(cache[i].mutex));
        cache[i].readcnt++;
        if ( cache[i].readcnt == 1 ) /* First in */
            P(&cache[i].w);
        V(&cache[i].mutex);

        // if there is an empty block
        if(cache[i].isEmpty) {
            // cache[i].isEmpty = 0;
            P(&cache[i].mutex);
            cache[i].readcnt--;
            if(cache[i].readcnt==0) /* Last out */
            V(&cache[i].w); 
            V(&cache[i].mutex);

            ret = i;
            break;
        }

        // search for the LRU block
        if(cache[i].stamp > max_stamp) 
        {  
            ret = i;
            max_stamp = cache[i].stamp;
        }
        
        P(&cache[i].mutex);
        cache[i].readcnt--;
        if( cache[i].readcnt == 0 ) /* Last out */
            V(&cache[i].w); 
        V(&cache[i].mutex);
    }

    return ret;
}

/**
 * update timestamps for all cache blocks
 * @param index the block currently being read or written
 * */
void cache_update(int idx)
{
    for(int i = 0; i < CACHE_OBJS_COUNT; ++i){
        if(cache[i].isEmpty == 0){
            P(&cache[idx].w);

            if(i != idx)
                cache[i].stamp++;
            else
                cache[i].stamp = 0;
            
            V(&cache[idx].w);
        }
    }
}

/**
 * cache a new object
 * @param url the cached object name
 * @param buf the content to be cached
 * */
void cache_put(char *url,char *buf)
{
    // find a proper block index
    int idx = cache_eviction(); 
    
    P(&cache[idx].w);
    
    strcpy(cache[idx].cache_url, url);
    strcpy(cache[idx].cache_obj, buf);
    cache[idx].isEmpty = 0;
    
    V(&cache[idx].w);

    cache_update(idx);
}

