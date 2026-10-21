
#include "csapp.h"
#include "http_parser.h"
#include "cache.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>


#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_BLOCKS (MAX_CACHE_SIZE/MAX_OBJECT_SIZE)

static block_t *start = NULL;
static block_t *end = NULL; //the last valid block
static int blocks = 0; //total number of blocks stored in the cache
static int lru_num = 0; //the total number of GET requests

//@param: a block struct
//remove the block from the cache list and free the memory used
//change the next, prev pointers of the previous and next blocks, change the 
//start and end of the cache if needed
void free_block(block_t *block) {
    fprintf(stderr, "*******trying to free block*******\n");
    block_t *prev_block = (block_t *)(block->prev);
    block_t *next_block = (block_t *)(block->next);
    if ((prev_block == NULL) && (next_block == NULL)) {
        //the only block in the list
        start = NULL;
        end = NULL;
        free(block);
    }
    else if ((prev_block == NULL) && (next_block != NULL)) {
        //the first block (but not the only)
        start = next_block;
        free(block);
    }
    else if ((prev_block != NULL) && (next_block == NULL)) {
        //the last block (but not the only)
        end = prev_block;
        free(block);
    }
    else {
        //the "normal case"
        prev_block->next = next_block;
        next_block->prev = prev_block;
        free(block);
    }
    blocks -= 1;
    fprintf(stderr, "*******done freeing block*******\n");
    return;
}

//@param: the URI of the request->key; the requested content->value
//malloc a new block, set its key, value, and lru_num
//@return: the newly built block
block_t *build_new_block (char key[MAXLINE], char value[MAX_OBJECT_SIZE]) {
    block_t *new_block = malloc(sizeof(block_t));
    memcpy(new_block->key, key, strlen(key));
    memcpy(new_block->value, value, strlen(value));
    new_block->lru_num = lru_num;
    return new_block;
}

//@param: start of the list (cache)
//find the block in the list with the least lru number
//return the block
block_t *find_lru(block_t *start) {
    int min_lru = -1;
    block_t *current = NULL;
    block_t *lru_block = NULL;
    for (current = start; current != NULL; current = (block_t *)(current->next)) 
    {
        if ((min_lru == -1) || (current->lru_num < min_lru)) {
            min_lru = current->lru_num;
            lru_block = current;
        }
    }
    return lru_block;
}

//@param: key and value of the new block
//add it to the end of the cache
//Special Cases: 1. If there are already a max number of blocks, then evict
//2. If the list was originally empty, both the start and end point to this new
//block
void add_new_block(char key[MAXLINE], char value[MAX_OBJECT_SIZE])
{   
    fprintf(stderr, "*******adding new blocks*******\n");
    if (blocks == MAX_BLOCKS) {
        //if the cache is already full, then we need to evict
        //but we are not adding more blocks to the cache
        block_t *evict_block = find_lru(start);
        if (evict_block->refcnt == 0) {
            //only evict if refcnt is 0 
           free(evict_block);
        }
        else {
            //if the block is currently being used by others, just give up 
            //saving this new block
            fprintf(stderr, "*******give up adding******\n");
            return;
        }
    }
    block_t *new_block = build_new_block(key, value);
    if ((start == NULL) && (end == NULL)) {
        //when the list is empty
        fprintf(stderr, "******adding first block******\n");
        start = new_block;
        end = new_block;
        new_block->prev = NULL;
        new_block->next = NULL;
    }
    else {
        end->next = new_block;
        new_block->prev = end;
        new_block->next = NULL;
        end = new_block;
    }
    blocks += 1;
    fprintf(stderr, "------blocks after: %d------\n", blocks);
    return;
}

//@param: key, uri of a request
//find a block with the same key if possible
//@return the block if found, NULL otherwise
block_t* find_block(char key[MAXLINE]) {
    fprintf(stderr, "*******trying to find block*******\n");
    block_t *current;
    for (current = start; current != NULL; current = (block_t *)(current->next)) 
    {
        if (!strncmp(current->key, key, strlen(current->key))) {
            //note strncmp() returns 0 if the two inputs match
            //if we've found an idential key
            current->lru_num = lru_num;
            return current;
        }
    }
    fprintf(stderr, "*******done finding block*******\n");
    return NULL;
}

//@param: the key (URI)
//search the whole list of blocks, increment the lru_num
//@return the block if one with the same key is found. Otherwise return NULL
block_t* cache(char key[MAXLINE]) {
    lru_num += 1;
    //everytime that cache() gets called, we are having a new GET request
    block_t *block;
    if ((block = find_block(key)) != NULL) {
        return block;
    }
    fprintf(stderr, "--------no block found------\n");
    return NULL;
}