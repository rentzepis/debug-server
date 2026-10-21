/*
 * cache.c
 *
 * Author: Kais Kudrolli
 * Andrew ID: kkudroll
 *
 * File Description: This file contains the implementations for the main
 * cache interface functions and their associated helper functions. These
 * functions allow a client to intialize a cache, perform a lookup for an 
 * object, strore objects in a cache, and free the cache. Reader/writer
 * locks from the pthread library are used to protect shared variables so
 * that the cache can be used in a concurrent program. The cache is used by
 * a proxy to store web objects received from a server so that if the same
 * content is requested again, it can be accessed more quickly because
 * it does not have to be retrieved again from a web server.
 *
 */

#include "cache.h"

/* 
 * Main Cache Functions
 * --------------------
 */

/*
 * cache_init - This function initializes a cache with two nodes: a start
 * and and end node. It returns a ponter to the start node, i.e. the start
 * of the linked list that implements the cache.
 *
 * Return value:
 *  - start: a pointer to the start of the cache (linked list) 
 */
Cache *cache_init(void) 
{
    Cache *start = Malloc(sizeof(Cache));
    Cache *end = Malloc(sizeof(Cache));

    /* 
     * Create start and end nodes. These act as dummy nodes on
     * the end of the linked list so that adding and removing 
     * nodes is simplified. That is there are always two nodes 
     * surrounding a node to be added or removed.
     */
    start->object_size = 0;
    start->lru_count = 0;
    strcpy(start->uri, "");
    strcpy(start->content, "");
    start->next = end;
    start->prev = NULL;

    end->object_size = 0;
    end->lru_count = 0;
    strcpy(end->uri, "");
    strcpy(end->content, "");
    end->next = NULL;
    end->prev = start;

    return start;
}

/*
 * cache_lookup - Searches for content in the cache by its key (URI).
 * If the content is found, the content paramter is filled with the content,
 * and a hit is returned. Otherwise, content is not filled, and a miss is
 * returned.
 *
 * Parameters:
 *  - cache: a pointer to the cache the function should search
 *  - uri: the uri of the content
 *  - content: a buffer that is filled with the content if there is a hit
 * Return Value:
 *  - 1: a hit, the content is in the cache
 *  - 0: a miss, the content is not in the cache
 */
int cache_lookup(Cache *cache, char *uri, char *content)
{
    /* 
     * Use a read lock to allow multiple readers or one writer
     * to access the function 
     */
    Pthread_rwlock_rdlock(&cache_lock);

    Cache *rover;
    int hit = 0;

    /* Search for the uri in the cache */
    for (rover = cache; rover != NULL; rover = rover->next) {
        /* Update LRU */
        rover->lru_count += 1;
        if (!strcmp(rover->uri, uri)) {
            /* The content is found */
            strncpy(content, rover->content, rover->object_size);
            rover->lru_count = 0;
            hit = 1;
        }
    }

    /* Unlock the cache lock */
    Pthread_rwlock_unlock(&cache_lock);
    return hit;
}   

/* 
 * cache_add - This function stores content in the cache. If adding the 
 * content would cause the size of the cache to exceed its limit, the LRU
 * node is removed until there is enough space in the cache for the new 
 * content. The new node is always added to the front of the list.
 *
 * Parameters:
 *  - cache: a pointer to the cache to which the content will be added
 *  - uri: the URI of the content
 *  - content: the actual object to be cached
 */
void cache_add(Cache *cache, char *uri, char *content)
{
    /*
     * Use a writer lock to prevent more than one writer or reader
     * from accessing this function at a time.
     */
    Pthread_rwlock_wrlock(&cache_lock);

    int cache_size = get_cache_size(cache);
    int content_size = strlen(content);
    int new_size = cache_size + content_size;
    int remove_size;

    /* 
     * Does not check for miss/hit. Due to how where cache_add is used 
     * in the proxy, there will always be a miss in add. Thus, checking
     * for a miss here would only slow down the add function.
     */

    /* Remove LRU nodes until there is enough space in the cache */
    while (new_size > MAX_CACHE_SIZE) {
        remove_size = new_size - MAX_CACHE_SIZE;
        remove_node(cache, remove_size);
        /* Update the size of the cache after a removal */
        new_size = get_cache_size(cache) + content_size;
    }
    add_node(cache, uri, content, content_size);

    /* Unlock the writer lock */
    Pthread_rwlock_unlock(&cache_lock);
    return;
}

/*
 * cache_destroy - This functions loops over the list and frees all the 
 * nodes.
 *
 * Parameter:
 *  - cache: the cache to be destroyed
 */
void cache_destroy(Cache *cache) 
{
    Cache *rover;

    while (cache != NULL) {
        rover = cache->next;
        Free(cache);
        cache = rover;
    }

    return;
}

/*
 * End Main Cache Functions
 * ------------------------
 */


/* 
 * Cache Helper Functions
 * ----------------------
 */

/*
 * get_cache_size - This function calculates the real size of the cache.
 * It loops over the cache and sums the object sizes.
 *
 * Parameter:
 *  - cache: pointer to the cache whose size is being found
 * Return value:
 *  - cache_size: the real size of the cache
 */
int get_cache_size(Cache *cache)
{
    int cache_size = 0;
    Cache *rover;

    /* Sum up all object sizes */
    for (rover = cache; rover != NULL; rover = rover->next) {
        cache_size += rover->object_size;
    }

    return cache_size;
}

/* add_node - This adds a node to a link list and initializes the fields
 * of the struct with the parameters given.
 *
 * Parameters:
 *  - cache: pointer to the cache to which we are adding a node
 *  - uri: URI of the content
 *  - content: buffer containing the content
 *  - object_size: size of the object (length of the content string)
 */
void add_node(Cache *cache, char *uri, char *content, int object_size)
{
    Cache *node = Malloc(sizeof(Cache));
    
    /* Initialize the struct fields */
    node->object_size = object_size;
    node->lru_count = 0;
    strcpy(node->uri, uri);
    strcpy(node->content, content);
    /* Link the node into the cache */
    node->next = cache->next;
    node->prev = cache;
    node->next->prev = node;
    cache->next = node;

    return;
}

/*
 * remove_node - This searches for a node in a linked list and removes it. 
 * The node is a LRU node with object_size greater than or equal to the 
 * remove_size.
 *
 * Parameters:
 *  - cache: pointer the cache from which a node will be removed
 *  - remove_size: minimum size of the node to be removed
 */
void remove_node(Cache *cache, int remove_size) 
{
    Cache *rover;
    Cache *rm_node;
    int highest_lru = -1;

    /* 
     * Finds the LRU node that is big enough to accommodate the new
     * block if removed.
     */
    for (rover = cache; rover != NULL; rover = rover->next) {
        if (rover->lru_count > highest_lru 
                && rover->object_size >= remove_size) {
            rm_node = rover;
        }
    }

    /* Unlink the node and free it */
    if (rm_node != NULL) {
        rm_node->next->prev = rm_node->prev;
        rm_node->prev->next = rm_node->next;
        Free(rm_node);
    }
    return;
}

/* 
 * print_cache - Print the whole cache. This is used for debugging.
 *
 * Parameter:
 *  - cache: pointer to te cache to be printed out
 */
void print_cache(Cache *cache)
{
    Cache *rover;
    int max_cache = MAX_CACHE_SIZE;
    int max_obj = MAX_OBJECT_SIZE;
    int node_count = 0;

    printf("MAX_CACHE: %d\n", max_cache);
    printf("MAX_OBJ: %d\n", max_obj);

    for (rover = cache; rover != NULL; rover = rover->next) {
        printf("Node %d: %p\n", node_count, rover);
        printf("Obj size: %d\n", rover->object_size);
        printf("lru: %d\n", rover->lru_count);
        printf("uri: %s\n", rover->uri);
        printf("content: %s\n", rover->content);
        printf("next: %p\n", rover->next);
        printf("prev: %p\n", rover->prev);
        node_count++;
    }
}

/* 
 * End Cache Helper Functions
 * --------------------------
 */


/* 
 * Pthread Warning Wrapper Functions
 * ---------------------------------
 * These warning wrapper catch errors in the pthread functions: init,
 * rdlock, wrlock, and unlock.
 */

int Pthread_rwlock_init(pthread_rwlock_t *rwlock, 
        const pthread_rwlockattr_t *attr)
{
    int rtn;

    if ((rtn = pthread_rwlock_init(rwlock, attr)) != 0) {
        fprintf(stderr, "Error in read/write init. Errno: %d.\n", rtn);
    }

    return rtn;
}

int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock)
{
    int rtn;

    if ((rtn = pthread_rwlock_rdlock(rwlock)) != 0) {
        fprintf(stderr, "Error in read/write read lock. Errno: %d.\n", rtn);
    }

    return rtn;
}

int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock)
{
    int rtn;

    if ((rtn = pthread_rwlock_wrlock(rwlock)) != 0) {
        fprintf(stderr, "Error in read/write write lock. Errno: %d.\n", rtn);
    }

    return rtn;
}

int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock)
{
    int rtn;

    if ((rtn = pthread_rwlock_unlock(rwlock)) != 0) {
        fprintf(stderr, "Error in read/write unlock. Errno: %d.\n", rtn);
    }

    return rtn;
}

/*
 * End Pthread Warning Wrapper Functions
 * -------------------------------------
 */
