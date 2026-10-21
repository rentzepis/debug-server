#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct cache_node {
    char *tag;
    void *data;
    int size;
    struct cache_node *next;
};

typedef struct cache_node cache_node;

typedef struct {
    cache_node *head;
    cache_node *rear;
    unsigned int remain_length;
    sem_t w_mutex;
    sem_t c_mutex;
    unsigned int readcnt;
} cache_t;

/* cache helper functions*/
void *init_cache(cache_t *c);
cache_node *find(cache_t *c, char *tag);
void insert(cache_t *c, cache_node *node);
cache_node *evict(cache_t *c, char *tag);
