
#include <pthread.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000  // 1 MiB
#define MAX_OBJECT_SIZE 102400  // 100 KiB

/*
This setup allows:
O(1) insert/remove
O(n) search by URL (you can improve it with a hash map if needed)
LRU eviction by popping from the tail

Thread-Safe Access Using Reader-Writer Lock
Use pthread_rwlock_t:
pthread_rwlock_rdlock(&lock) for concurrent readers
pthread_rwlock_wrlock(&lock) for writers (insert/remove)
*/
typedef struct CacheBlock
{
    char *url;                // Unique key
    char *content;            // The cached web object
    int size;                 // Size of the object in bytes
    struct CacheBlock *prev; // For LRU: older
    struct CacheBlock *next; // For LRU: newer
} CacheBlock;

typedef struct
{
    CacheBlock *head; // Most recently used
    CacheBlock *tail; // Least recently used
    int total_size;
    pthread_rwlock_t lock; // Reader-writer lock for thread-safe access
} CacheList;

CacheList * cache_build();

void cache_free(CacheList *cache);

char *cache_get(CacheList *cache, const char *url, int *object_size);


int cache_put(CacheList *cache, const char *url, const char *buf, int size);
void Cache_put(CacheList *cache, const char *url, const char *buf, int size);