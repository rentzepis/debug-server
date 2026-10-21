/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 10970
#define MAX_OBJECT_SIZE 10970

typedef struct cache_entry {
    char *key;
    char *value;
    int size;
    struct cache_entry *prev;
    struct cache_entry *next;
} cache_entry;

void cache_init();
cache_entry *cache_find(char *key);
void cache_insert(char *key, char *value, int size);
void cache_evict(int size);
void cache_free();