/*
 * handle caching
 */

#include "cache.h"


void *init_cache(cache_t *c) {

    c->head = NULL;
    c->rear = NULL;
    c->remain_length = MAX_CACHE_SIZE;

    Sem_init(&c->w_mutex, 0, 1);
    Sem_init(&c->c_mutex, 0, 1);

    c->readcnt = 0;
}

cache_node *find(cache_t *c, char *tag) {
    cache_node *temp_node = c->head;
    int i;
    
    P(&(c->w_mutex));
    c->readcnt++;
    if (c->readcnt == 1) {
        P(&(c->c_mutex));
    }
    V(&(c->w_mutex));


    for (i = 0; temp_node != NULL; i++) {
        if (!strcmp(temp_node->tag, tag) ) {
            return temp_node;
        }
        temp_node = temp_node->next;
    }

    P(&(c->w_mutex));
    c->readcnt --;
    if (c->readcnt == 0) {
        V(&(c->c_mutex));
    }
    V(&(c->w_mutex));

    return NULL;
}

/*
 * insert - Add a cache node to the rear of cache 
 */
void insert(cache_t *c, cache_node *node) {
    if (c->head == NULL) {
        c->head = c->rear = node;
        c->remain_length -= node->size;
    } else {
        c->rear->next = node;
        c->rear = node;
        c->remain_length -= node->size;
    }
}

cache_node *evict(cache_t *c, char *tag) {
    cache_node *prev_node = NULL;
    cache_node *cur_node = c->head;
    
    while (cur_node != NULL) {
        if (!strcmp(cur_node->tag, tag)) {
            if (c->head == cur_node) {
                c->head = cur_node->next;
            }

            if (c->rear == cur_node) {
                c->rear = prev_node;
            }

            if (prev_node != NULL) {
                prev_node->next = cur_node->next;
            }

            cur_node->next = NULL;
            c->remain_length += cur_node->size;
            return cur_node;
        }
        prev_node = cur_node;
        cur_node = cur_node->next;
    }
    
    return NULL;
}


