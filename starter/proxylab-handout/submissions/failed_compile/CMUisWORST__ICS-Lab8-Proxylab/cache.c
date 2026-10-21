/**
 * cache->c - functions about proxy cache
 */

#include "csapp.h"
#include "cache.h"
#include "proxy.h"

/**
 * init_cache - initiate cache->available_size = MAX_CACHE_SIZE
 */
void init_cache(Cache* cache){
    cache->obj_num = 0;
    cache->available_size = MAX_CACHE_SIZE;
}

/**
 * find_in_cache - look for a object in cache by its uri
 */
void* find_in_cache(char* uri, Cache* cache){

    sem_wait(&rdmutex); // operate on semaphore
    readcnt++;
    if(readcnt == 1)
        sem_wait(&wtmutex);
    sem_post(&rdmutex);

    dbg_printf("***finding in cache***\n");
    int i;
    for(i = 0; i < cache->obj_num; ++i){
        if(cache->object[i] && !strcmp(uri, cache->object[i]->uri)){
            dbg_printf("cache found.\n");

            sem_wait(&rdmutex);
            readcnt--;
            if(readcnt == 0)
                sem_post(&wtmutex);
            sem_post(&rdmutex);     // operate on semaphore

            return cache->object[i];
        }
    }
    dbg_printf("cache miss.\n");                    

    sem_wait(&rdmutex);
    readcnt--;
    if(readcnt == 0)
        sem_post(&wtmutex);
    sem_post(&rdmutex);     // operate on semaphore

    return NULL;
}

/**
 * insert_cache - insert a cache_object into cache list
 */
void insert_cache(Cache* cache, cache_object* obj){

    sem_wait(&wtmutex);     // operate on semaphore

    dbg_printf("***in insert_cache***\n");
    if(obj->obj_size > MAX_OBJECT_SIZE) {
        dbg_printf("object too large, unable to save in cache:(\n");

        sem_post(&wtmutex);
        return;
    }
    while(cache->available_size < MAX_OBJECT_SIZE ||
        cache->obj_num >= MAX_OBJ_NUM)
    {
        delete_cache(cache);
    }
    obj->recently_use = ++timestamp;
    cache->object[cache->obj_num++] = obj;
    cache->available_size -= obj->obj_size;
    dbg_printf("cache available size = %d bytes\n", cache->available_size);
    dbg_printf("cache obj_num = %d\n", cache->obj_num);
    dbg_printf("insert finished.\n");

    if(cache->available_size < 0){
        dbg_printf("cache->available_size = %d\n", cache->available_size);
        dbg_printf("cache->obj_num = %d\n", cache->obj_num);
        exit(1);
    }

    sem_post(&wtmutex);     // operate on semaphore
}

/**
 * delete_cache - delete an object from cache list
 *      it can only be called by insert_cache() in this implementation.
 */
void delete_cache(Cache* cache){
    dbg_printf("***in delete_cache***\n");
    int i, select = -1, release_bytes = 0;
    unsigned minTime = 0;
    for(i = 0; i < cache->obj_num; ++i){
        if(cache->object[i]){
            minTime = cache->object[i]->recently_use;
            select = i;
            break;
        }
    }
    for( ; i < cache->obj_num; ++i){
        if(cache->object[i] && minTime > cache->object[i]->recently_use){
            minTime = cache->object[i]->recently_use;
            select = i;
        }
    }
    if(select < 0){
        dbg_printf("Couldn't find any objects to delete, delete failed.\n");
        return;
    }
    cache->available_size += cache->object[select]->obj_size;
    release_bytes = cache->object[select]->obj_size;
    Free(cache->object[select]->file);
    cache->object[select]->file = NULL;
    Free(cache->object[select]);
    cache->object[select] = NULL;
    cache->obj_num--;
    dbg_printf("%d bytes released, delete finished.\n", release_bytes);
}

/**
 * forward_cache_content - forward cache content to client
 */
void forward_cache_content(int clientfd, Cache* cache, cache_object* cache_obj)
{
    sem_wait(&rdmutex); // operate on semaphore
    readcnt++;
    if(readcnt == 1)
        sem_wait(&wtmutex);
    sem_post(&rdmutex);

    dbg_printf("***in forward_cache_content***\n");
    dbg_printf("Writing cache to client...\n");
    rio_writen(clientfd, cache_obj->header, strlen(cache_obj->header));
    rio_writen(clientfd, cache_obj->file, cache_obj->filesize);
    dbg_printf("Write finished.\n");

    sem_wait(&rdmutex);
    readcnt--;
    if(readcnt == 0)
        sem_post(&wtmutex);
    sem_post(&rdmutex);     // operate on semaphore
}

/**
 * make_new_cache - use deep copy to make a file and some descriptive info
 *      into a cache object
 */
void make_new_cache(Cache* cache, char* file, char* uri,
    char* header, int filesize)
{
    cache_object* new_cache = (cache_object*)Malloc(sizeof(cache_object));
    new_cache->file = (char*)Malloc(filesize);
    strcpy(new_cache->uri, uri);
    strcpy(new_cache->header, header);
    dbg_printf("new_cache->file = %p\n", new_cache->file);
    strncpy(new_cache->file, file, filesize);   // deep copy
    new_cache->filesize = filesize;
    new_cache->recently_use = ++timestamp;
    new_cache->obj_size = sizeof(cache_object) + filesize;
    insert_cache(cache, new_cache);
}