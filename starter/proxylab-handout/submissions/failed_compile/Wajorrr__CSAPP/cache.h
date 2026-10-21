#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct cache_block
{
    char url[MAXLINE];
    char content[MAX_OBJECT_SIZE];
    int content_size;
    int last_used;
} cache_block;

typedef struct cache_t
{
    cache_block blocks[MAX_CACHE_SIZE / MAX_OBJECT_SIZE];
    int block_count;
    int capacity;
} cache_t;

// 缓存，定义为全局变量
static cache_t cache;
// 信号量，分别用于实现全局变量并发锁、读写锁
static sem_t mutex, rw_mutex;
// 线程共享变量
static int readcnt, timestamp;

void cache_init();
int find_cache(rio_t *rio, char *url);
void insert_cache(char *url, char *content, int content_size);