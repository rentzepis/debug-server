#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define HASH_TABLE_SIZE 100

typedef struct CacheEntry
{
    char* url;
    void* data;
    size_t data_length;
    time_t last_accessed;

    struct CacheEntry* prev;
    struct CacheEntry* next;
    struct CacheEntry* hash_table_next;
} CacheEntry;

typedef struct Cache
{
    CacheEntry* hash_table[HASH_TABLE_SIZE];
    CacheEntry* head;
    CacheEntry* tail;
    size_t used_size;
} Cache;

/* CacheEntry begin */

CacheEntry* createCacheEntry(const char* url, const void* data, size_t data_length);
void freeCacheEntry(CacheEntry* entry);


/* CacheEntry end */

/* Cache begin */

void initCache(Cache* cache);
void freeCache(Cache* cache);
void removeEntry(Cache* cache, CacheEntry* entry);
void evictEntry(Cache* cache);
void makeSpace(Cache* cache, size_t target_space);
unsigned int hash(const char* str);
void appendEntry(Cache* cache, CacheEntry* entry);
void moveEntryToFront(Cache* cache, CacheEntry* entry);
CacheEntry* readEntry(Cache* cache, const char* url);

/* Cache end */