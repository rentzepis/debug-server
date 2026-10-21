/******************
 *  Cache Packet
 *****************/
#include "cache.h"

static unsigned evict(cache_t *header);
static struct ListNode *remove_node(struct ListNode *node);
static void insert_head(struct ListNode *head, struct ListNode *node);

/* cache_init - inite this cache */
void cache_init(cache_t *cache) {
    cache->head = (struct ListNode *) Malloc(sizeof(struct ListNode));
    cache->tail = (struct ListNode *) Malloc(sizeof(struct ListNode));
    cache->head->prev = NULL;
    cache->head->next = cache->tail;
    cache->tail->prev = cache->head;
    cache->tail->next = NULL;
    cache->size = 0;
    /* inite for synchronization */
    cache->readcnt = 0;
    Sem_init(&cache->mutex, 0, 1);
    Sem_init(&cache->write, 0, 1);
}

/* cache_read - find a block which is specified by uri path PATH, and
 * copy it to the USRBUF, if not find such block, then return -1
 * if everything is correct, then return the length of block */
int cache_read(cache_t *cache, char *path, void *usrbuf) {
    struct ListNode *node;
    int length;

    /* prepare for enter critical section */
    sem_wait(&cache->mutex);
    if (cache->readcnt == 0) {
        sem_wait(&cache->write);
    }
    cache->readcnt += 1;
    sem_post(&cache->mutex);

    if ((node = cache_search(cache, path)) == NULL) { 
        /* if not find such cache, then return -1 */
        length = -1;        /* P.S. there cannot return -1 directly */
    } else {
        memcpy(usrbuf, node->block, node->blocklen);
        length = node->blocklen;
    }

    /* prepare for leave critical section */
    sem_wait(&cache->mutex);
    cache->readcnt -= 1;
    if (cache->readcnt == 0) {
        sem_post(&cache->write);
    }
    sem_post(&cache->mutex);
    return length;
}

/* cache_write - write the buffer USRBUF to the cache, if the cache do
 * not have enough space, then need evict some blocks 
 * P.S. assume the BLOCKLEN is less or equal than MAX_OBJECT_SIZE(100 Kb) */
void cache_write(cache_t *cache, char *path, unsigned pathlen, void *usrbuf, 
            unsigned blocklen) {
    struct ListNode *node;
    unsigned len;

    /* prepare for enter critical section */
    sem_wait(&cache->write);

    while (blocklen + cache->size > MAX_CACHE_SIZE) {
        /* need evict some blocks */
        len = evict(cache);
        cache->size -= len;
    }
    /* alloc a new block */
    node = (struct ListNode *) Malloc(sizeof(struct ListNode));
    node->path = (char *) Malloc(pathlen+1);
    node->block = (void *) Malloc(blocklen);
    strcpy(node->path, path);
    memcpy(node->block, usrbuf, blocklen);
    node->blocklen = blocklen;
    insert_head(cache->head, node);

    /* prepare for leave critical section */
    sem_post(&cache->write);
}

/* cache_search - find a block which is specified by uri path PATH,
 * if find such block, then return an address point to this node,
 * else return NULL */
struct ListNode *cache_search(cache_t *cache, char *path) {
    struct ListNode *ptr;

    for (ptr = cache->head->next; ptr != cache->tail; ptr = ptr->next) {
        if (strcmp(ptr->path, path) == 0) {
            /* if find such cache, then update it */
            insert_head(cache->head, remove_node(ptr));
            return ptr;
        }
    }
    return NULL;
}

/* evict - evict the LRU block, and then return the length of it */
static unsigned evict(cache_t *header) {
    struct ListNode *node;
    unsigned length;

    node = remove_node(header->tail->prev);
    length = node->blocklen;
    /* free this block */
    free(node->block);
    free(node->path);
    free(node);
    return length;
}

/* remove - remove this node from link list, and return this node */
static struct ListNode *remove_node(struct ListNode *node) {
    struct ListNode *prev, *next;

    prev = node->prev;
    next = node->next;
    prev->next = next;
    next->prev = prev;
    return node;
}

/* insert_head - insert this node just after the head node */
static void insert_head(struct ListNode *head, struct ListNode *node) {
    struct ListNode *next;

    next = head->next;
    head->next = node;
    node->next = next;
    next->prev = node;
    node->prev = head;
}
