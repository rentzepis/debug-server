#ifndef CACHE_H
#define CACHE_H
#define MAXLINE 8192
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)
#define MAX_BLOCKS (MAX_CACHE_SIZE/MAX_OBJECT_SIZE)

typedef struct {
    int lru_num; //the number of the last operation involved
    int refcnt;
    char key[MAXLINE]; //the URL key
    char value[MAX_OBJECT_SIZE]; //the value, requested content
    void *next;
    void *prev;
} block_t;

//the cache would be a doubly linked list of block_t*
block_t* cache(char key[MAXLINE]);

void free_block(block_t *block);

void add_new_block(char key[MAXLINE], char value[MAXLINE]);

block_t *find_block(char key[MAXLINE]);

block_t *find_lru(block_t *start);

#endif /* CACHE_H */