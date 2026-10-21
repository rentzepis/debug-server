#include <stddef.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"

static void delete_head(cache_t *cachep);
static void insert_node(cache_t *cachep, node_t *node);
static void move_to_tail(cache_t *cachep, node_t *node);/*move to front and set the head pointer to node*/

void init_cache(cache_t *cachep)
{
    cachep->size = 0;
    cachep->head = cachep->tail = NULL; 
    cachep->readcnt = 0;
    Sem_init(&cachep->mutex, 0, 1);
    Sem_init(&cachep->w, 0, 1);
}

// write/insert at the head, n is the size of value in bytes
void insert_cache(cache_t *cachep, char *key, char *val, int n)
{
    /*shared memory on heap*/
    node_t *node = Malloc(sizeof(node_t));
    node->key = Malloc((strlen(key) + 1)*sizeof(char));
    node->val = Malloc(n*sizeof(char));
    node->prev = NULL;
    node->next = NULL;
    node->n = n;
    strcpy(node->key, key);
    memcpy(node->val, val, n);
    /*insert at the tail of the lst*/
    P(&cachep->w);
    cachep->size += n;
    insert_node(cachep, node);
    /*check the total size*/
    while (cachep->size > MAX_CACHE_SIZE){ /*evict until meet the size*/
       delete_head(cachep);
    }
    V(&cachep->w);
}

/*put the val at addr val and return the length of the val size in bytes*/
int get_cache(cache_t *cachep, char *key, char *val){
    /*search the lst*/
    P(&cachep->mutex);
    cachep->readcnt++;
    if (cachep->readcnt == 1){
        P(&cachep->w);
    }
    V(&cachep->mutex);
    node_t *curr;
    int res = 0;
    for (curr = cachep->head;curr != NULL;curr = curr->next) {
        if (!strcmp(key, curr->key)){
            memcpy(val, curr->val, curr->n);
            res = curr->n;
            break;
        }
    }

    P(&cachep->mutex);
    cachep->readcnt--;
    /* last reader move the current node to front*/
    if (cachep->readcnt == 0){
        if (res != 0) {
            move_to_tail(cachep, curr);
        }
        V(&cachep->w);
    }
    V(&cachep->mutex);
    return res;
}

static void insert_node(cache_t *cachep, node_t *node)
{
    if (cachep->tail == NULL) {
        cachep->head = node;
        cachep->tail = node;
    } else {
        cachep->tail->next = node;
        node->prev = cachep->tail;
        cachep->tail = node;
    }
}

static void delete_head(cache_t *cachep)
{
    node_t *tmp = cachep->head;
    cachep->size -= tmp->n;
    cachep->head = cachep->head->next;
    cachep->head->prev = NULL;
    Free(tmp->key);
    Free(tmp->val);
    Free(tmp); 
}

static void move_to_tail(cache_t *cachep, node_t *curr)
{
    if (cachep->tail == curr) {
        return;
    }
    curr->next->prev = curr->prev;
    if (curr->prev != NULL){
        curr->prev->next = curr->next;
    } else {
        cachep->head = curr->next;
    }
    curr->next = NULL;
    curr->prev = cachep->tail;
    cachep->tail = curr;
}