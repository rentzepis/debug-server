#include <stdio.h>
#include "./csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_OBJECT_NUM 10

typedef struct {
    char url[MAXLINE];
    char data[MAX_OBJECT_SIZE];
    size_t size;
    int timestamp;
    //int is_empty;
} cache_block;

typedef struct {
    int current_cache_num;
    cache_block cache_list[MAX_OBJECT_NUM];
} cache_t;


void init_cache();
void add_cache(char *url, const char *buf, size_t size);
int query_cache(char *url, rio_t client_rio);
