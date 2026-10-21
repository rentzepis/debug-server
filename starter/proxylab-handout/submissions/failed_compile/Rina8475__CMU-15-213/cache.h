/******************
 *  Cache Packet
 *****************/
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Structure of link list node */
struct ListNode {
    struct ListNode *prev;      /* point to previous block */
    struct ListNode *next;      /* point to next block */
    char        *path;          /* point to URI path of block */
    void        *block;         /* point to block */
    unsigned    blocklen;       /* record the length of block */
};

/* Structure of link list header */
struct ListHead {
    struct ListNode *head;      /* point to the header of link list */
    struct ListNode *tail;      /* point to the tailer of link list */
    unsigned size;              /* record the sum of size of all the blocks */
    int     readcnt;            /* record the number of thread is reading */
    sem_t   mutex;              /* Protects accesses to readcnt */
    sem_t   write;              /* Protects accesses to link list */
};

typedef struct ListHead cache_t;

void cache_init(cache_t *cache);
int cache_read(cache_t *cache, char *path, void *usrbuf);
void cache_write(cache_t *cache, char *path, unsigned pathlen, void *usrbuf, 
            unsigned blocklen);
struct ListNode *cache_search(cache_t *header, char *path);
