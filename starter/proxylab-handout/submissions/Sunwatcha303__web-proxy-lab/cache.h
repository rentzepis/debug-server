#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#include "csapp.h"

#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)

typedef struct block_t
{
    char *key;
    unsigned char *response;
    size_t response_size;
    struct block_t *next;
    struct block_t *prev;
    int last_access;
} block_t;

typedef struct
{
    size_t size;
    block_t *head;
    block_t *tail;
    pthread_mutex_t lock;
} cache_t;

void init_cache(void);
block_t *find_block(const char *key);
int insert_block(const char *key, const unsigned char *response, size_t response_size);
void close_cache(void);
void print_cache_stats(void);

#endif // CACHE_H