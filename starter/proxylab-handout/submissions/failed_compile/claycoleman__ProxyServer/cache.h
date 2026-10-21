// @Copyright 2017 Scott Leland Crossen

#include "csapp.h"

// Defined in class handout
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// An object stored in cache. Private. A pointer is returned to data in methods.
typedef struct cache_object_t {
    char* id;
    void* data;
    struct cache_object_t* next;
    unsigned int length;
} cache_object_t;

// The data structure containing the cache for the proxy
typedef struct cache_t {
    cache_object_t* head;
    cache_object_t* tail;
    sem_t read;
    sem_t write;
    unsigned remaining_length;
    unsigned int read_count;
} cache_t;

// Methods available for modifying the cache
cache_t* init_cache();
int add_data_to_cache(cache_t* queue, char* id, void* content, unsigned int length);
int read_data_from_cache(cache_t* queue, char* id, void* content, unsigned int* length);
