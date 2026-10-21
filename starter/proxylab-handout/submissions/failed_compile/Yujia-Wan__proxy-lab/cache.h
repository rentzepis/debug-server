/**
 * @file cache.h
 * @brief Interface for caching web objects
 *
 * @author Yujia Wang <yujiawan@andrew.cmu.edu>
 */

#include "csapp.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <unistd.h>

/*
 * Max cache and object sizes
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

/**
 * @brief Cache block structure
 */
typedef struct cache_block {
    char *url;
    char *object;
    ssize_t object_size;
    unsigned long reference_count;
    struct cache_block *next;
    struct cache_block *prev;
} cache_block_t;

/**
 * @brief Cache structure - doubly linked list
 */
typedef struct cache {
    cache_block_t *head;
    cache_block_t *tail;
    ssize_t size;
} cache_t;

/**
 * @brief Initialize a new cache
 */
void init_cache();

/**
 * @brief Free all memory used by cache
 */
void free_cache();

/**
 * @brief Allocate memory for a cache block
 * @param[in] uri URI of GET request
 * @param[in] obj Web object
 * @param[in] obj_size Size of web object
 * @return Pointer to the allocated cache block
 */
cache_block_t *alloc_block(const char *uri, char obj[], ssize_t obj_size);

/**
 * @brief Free all memory used by a cache block
 * @param block Cache block to be freed
 */
void free_block(cache_block_t *block);

/**
 * @brief Insert the cache block to the head of the list
 * @param block Cache block to be inserted
 */
void insert_head(cache_block_t *block);

/**
 * @brief Remove the tail cache block of the list
 */
void remove_tail();

/**
 * @brief Retrieve cache to check if the URL is in cache
 * @param[in] uri URI of GET request
 * @param[in] fd Connected descriptor
 * @return Size of web object of the URL
 * @return -1 if the URL is not found
 */
ssize_t read_cache(const char *uri, int fd);

/**
 * @brief Store a new web object in cache with its key
 * @param[in] uri URI of GET request
 * @param[in] obj Web object
 * @param[in] obj_size Size of web object
 */
void write_cache(const char *uri, char object[], ssize_t object_size);

/**
 * @brief Helper function to check correctness of cache
 */
void print_cache();
