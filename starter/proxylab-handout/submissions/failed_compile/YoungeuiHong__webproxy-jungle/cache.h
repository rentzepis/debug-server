#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

/* Recommended max cache and object sizes */

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


/* 캐시 노드 */
typedef struct cache_node node;
struct cache_node
{
    int size;
    char* url;
    char* data;
    node *next;
    node *prev;
    
};

/* 캐시 리스트 (이중 연결 리스트) */
typedef struct cache_header cache;
struct cache_header
{
    node *front;
    node *end;
    int empty_size;
    sem_t mutex_r, mutex_w;
    int readcnt
};

cache* init_cache();

node* new_node(char* url, char* data, int length);

void delete_node(cache* list, node* p);

void free_node(node* p);

node* search_node(cache* list, char* url);

void insert_node_front(cache* list, node* p);

void insert_node_end(cache* list, node* p);

node* find_cache(cache* list, char* url);

void add_cache(cache* list, char* url, char* data, int length);

void evict_cache(cache* list, node* p);