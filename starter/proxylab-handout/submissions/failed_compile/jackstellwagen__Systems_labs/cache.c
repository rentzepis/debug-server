 /**
 * @file cache.c
 * @brief A multithreaded LRU cache implementation mean to be run with proxy.c
 *
 * The cache is implemented as a linked list where the head is the 
 * most recently used entry and the tail is the least.
 * 
 * When calling functions that modify or rely on the linked list, the caller 
 * should lock/unlock the mutex cache_lock. Specifically when calling insert
 * and in_cache.
 * 
 * If a new enty does not fit in the cache the least recently used cache block
 * will be evicted until there is space. This may be space inefficient if the LRUs
 * are large
 *
 * 
 *
 * @author Jack Stellwagen <jstellwa@andrew.cmu.edu>
 */




#include "cache.h"

#define MAX_OBJECT_SIZE (100 * 1024) 
#define MAX_CACHE_SIZE (1024 * 1024)




size_t cache_size;

cache_block_t *head;
cache_block_t *tail;




/**
 * @brief Initializes cache variable
 * 
 * sets size to 0, head and tail to NULL, and 
 * initialized the mutex.
 * 
 */

void init_cache(){
    head = NULL;
    tail = NULL;
    cache_size = 0;
    pthread_mutex_init(&cache_lock, NULL);

}



/**
 * @brief Free the given block
 * 
 * Frees all allocated variables within the struct.
 * 
 * If refcount is not 0, meaning the payload is in use,
 * the block and payload will not be freed and the block
 * will have the attribute dead set to true. The remaining
 * allocated pointers will be freed by decrement_ref when 
 * refcount becomes 0.
 *
 */
void free_block(cache_block_t *block){
    pthread_mutex_lock(&(block->ref_lock));

    free(block->host);
    free(block->path);
    free(block->port);

    if (block->refcnt == 0){
        free(block->payload);
        pthread_mutex_unlock(&(block->ref_lock));
        free(block);

    }else{
        block->dead = true;
        pthread_mutex_unlock(&(block->ref_lock));
    }


}



/**
 * @brief Frees the entire cache including all alloced
 * entities
 *
 */
void free_cache(){
    cache_block_t *block = head;
    cache_block_t *next;

    while(block != NULL){
        next = block->next;

        free_block(block);
        block = next;
    }

}



/**
 * @brief Insert the given block at the head of the linked list
 * 
 * It is assumed that the block is already fully removed from the list 
 * and that the entries in prev and next can be written over.
 * 
 */
void insert_head(cache_block_t *block){
    if (head == NULL){
        head = block;
        tail = block;
        block->prev = NULL;
        block->next = NULL;
    }
    else{
        head->prev = block;
        block->next = head;
        block->prev = NULL;
        head = block;
    }
}



/**
 * @brief Moves a cache block to the head of the linked list
 * 
 * To be called after a cache hit.
 * Moving a block to the head of the linked list 
 * updates when it was last visited
 * 
 *
 */
void visit(cache_block_t *block){
    cache_block_t *prev = block->prev;
    cache_block_t *next = block->next;

    //remove block from original place
    if (block == head){
        return;
    }
    else if (block == tail){
        prev->next = NULL;
        tail = prev;
    }
    else {
        //both are safe since prev,next are only NULL when head/tail
        prev->next = next;
        next->prev = prev;
    }

    //insert block at the head

    insert_head(block);

}


/**
 * @brief Increments a blocks refcount by 1
 * 
 */
void increment_ref(cache_block_t *block){
    pthread_mutex_lock(&(block->ref_lock));
    block->refcnt += 1;
    pthread_mutex_unlock(&(block->ref_lock));

}

/**
 * @brief Decrements the refcount of a given block by 1
 * 
 * If the block has already been removed from the cache and 
 * the paylad is no longer in use, the payload and block will
 * be freed
 * 
 *
 */
void decrement_ref(cache_block_t *block ){
    pthread_mutex_lock(&(block->ref_lock));
    block->refcnt -= 1;
    if (block->dead && block->refcnt ){
        free(block->payload);
        free(block);
    }
    pthread_mutex_unlock(&(block->ref_lock));
}


/**
 * @brief determines if the specified block is in the cache
 * host, port, and path must be NULL terminated strings
 *
 * match_block is passed by refence. If the desired block 
 * is present in the cache, match_block will be set equal 
 * to a pointer to the block
 *
 * @return 1 if block is present in cache, 0 otherwise
 *
 */
int in_cache(const char *host, const char *port, const char *path, 
                cache_block_t **match_block){
    cache_block_t *block = head;
    while(block != NULL){
        int same_host = strncmp(host, block->host, MAXLINE);
        int same_port = strncmp(port, block->port, MAXLINE); 
        int same_path = strncmp(path, block->path, MAXLINE);
        if(!same_host && !same_port && !same_path ){
            visit(block);
            increment_ref(block);
            *match_block = block;
            return 1;
        }
      
        block = block->next;

    }
    return 0;
}


/**
 * @brief Removes the least recently used entry from the cache
 * 
 * The LRU is the tail of the linked list
 * 
 *
 */
void removeLRU(){
    cache_block_t *removed = tail;

    if (tail == NULL) return;

    (tail->prev)->next = NULL;

    tail = tail->prev;

    cache_size -= removed->size;

    free_block(removed);

}


/**
 * @brief Given a the size of an object that needs to be inserted
 * the LRU entry is removed until the new object has space
 * 
 */
void makeSpace(size_t space){
    while(MAX_CACHE_SIZE - cache_size <= space){
        removeLRU();
    }
}


/**
 * @brief Insert a new entry into the cache
 * 
 * Repeats are accounted for so the entry does not necessarily
 * have to be new
 * 
 * path, port, and host must be null terminated
 * 
 * payload does not need to be null terrminated but must be 
 * shorter that MAX_OBJECT_SIZE
 *
 *
 */

void insert(const char *path, const char *port, const char *host, 
            char* payload, size_t payload_size){
    
    cache_block_t *match_block;
    if (in_cache(host,port,path,&match_block)){
        decrement_ref(match_block);
        return;
    }
    makeSpace(payload_size);

    char *new_payload = malloc(payload_size);
    memcpy(new_payload, payload, payload_size);


    //path, port, and host are null terminated returned by parser
    int port_len = strlen(port);
    char *new_port = malloc(port_len +1);
    memcpy(new_port, port, port_len +1);

    int path_len = strlen(path);
    char *new_path = malloc(path_len +1);
    memcpy(new_path, path, path_len +1);

    int host_len = strlen(host);
    char *new_host = malloc(host_len +1);
    memcpy(new_host, host, host_len +1);


    cache_block_t *block = malloc(sizeof(cache_block_t));

    block->port = new_port;
    block->payload = new_payload;
    block->size = payload_size;
    block->host = new_host;
    block->path = new_path;
    block->dead = false;

    pthread_mutex_init(&(block->ref_lock), NULL);

    cache_size += block->size;

    insert_head(block);
}
