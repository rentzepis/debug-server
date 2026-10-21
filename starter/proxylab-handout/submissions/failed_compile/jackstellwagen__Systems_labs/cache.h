
#include "csapp.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_OBJECT_SIZE (100 * 1024) 
#define MAX_CACHE_SIZE (1024 * 1024)


typedef struct block {

    size_t size;            //size of the payload
    char *payload;          //data from the server
    char *host;             //server hostname
    char *path;             //server path
    char *port;             //server porrt
    struct block *prev;     //previous block in linked list
    struct block *next;     //next block in linked list
    int refcnt;             //number of threads currectly using the payload
    bool dead;              //if variables have been freed but the payload 
                            //has not since it is still in use
    pthread_mutex_t ref_lock;   //lock for the reference counter

} cache_block_t;

pthread_mutex_t cache_lock;



void free_block(cache_block_t *block);
void init_cache(void);
void insert_head(cache_block_t *block);
void visit(cache_block_t *block);
int in_cache(const char *host, const char *port, const char *path, cache_block_t **match_block);
void removeLRU(void);
void makeSpace(size_t space);
void insert(const char *path, const char *port, const char *host, char* payload, size_t payload_size);
void increment_ref(cache_block_t *block);
void decrement_ref(cache_block_t *block);
void free_cache(void);
