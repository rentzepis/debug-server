/*
 * Andrew: assathe
 * Header file cache. contains cache line and cache structures
 * Forward declaration of public functions
 */

#include <stdbool.h>    
#include <stddef.h>     

#define MAX_CACHE_SIZE (1024*1024)

//cache line structue
typedef struct node {
    char *key; //uri
    char *object; //server response
    bool is_transmitting; //set if transmitting via socket
    size_t node_size; //response size
    struct node *prev; //pointer to prev cache line
    struct node *next; //pointer to next cache line
} node;

//cache structure - doubly linked list
typedef struct {
    size_t size; //cache size
    node *head; //cache head
    node *tail; //cache tail
} queue;


//public functions
queue *create_cache();
void free_cache(queue *cache);
node *extract_from_cache(queue *cache, char *key);
void insert_to_cache(queue *cache, char* key, char* object, size_t node_size);

