#include <semaphore.h>


typedef struct {
    int active;
    char *key;
    int value_length;
    void *value;
} entry_t;

typedef struct {
    sem_t lock;
    int length;
    int max_cache_size;
    int curr_cache_size;
    int max_entry_size;
    entry_t **entries;
} cache_t;

int hash(char *url, int);
void cache_init(cache_t *cp, int entry_num, int cache_size, int entry_size);
void cache_destruct(cache_t *cp);
void cache_show(cache_t *cp);
int cache_search(cache_t *cp, char *url);
void cache_evict(cache_t *cp, char *url);
void cache_add(cache_t *cp, char *url, void *mem_src, int length);
void cache_acquire_lock(cache_t *cp);
void cache_release_lock(cache_t *cp);
