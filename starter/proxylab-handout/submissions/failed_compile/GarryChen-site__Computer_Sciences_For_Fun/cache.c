
#include "cache.h"

#include "wr.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_LEN 50
Cache_Set cache_s;

// 
void *init_cache(Cache_Set *cache_s)
{
    cache_s->size = 0;
    cache_s->cache = malloc(CACHE_LEN * (sizeof(GroupNode)));
    memset(cache_s->cache, 0, CACHE_LEN * (sizeof(GroupNode)));
}

static inline int find_cache(Cache_Set *cache_s, struct Key *key)
{

    lock_reader();
    size_t cnt = cache_s->size;
    Cache cache = cache_s->cache;

    int i = 0;
    for (; cache[i].t; ++i)
    {
        // printf("%s-------%s\n", cache[i].key.host, cache[i].key.path);
        if (!strcmp(key->host, cache[i].key.host) && key->port == cache[i].key.port &&
            !strcmp(key->path, cache[i].key.path))
            break;
    }
    unlock_reader();

    if (cache[i].t)
    {
        lock_writer();
        cache[i].t = clock();
        unlock_writer();
    }
    return i;
}

char *read_cache(Cache_Set *cache_s, struct Key *key)
{

    int pos = find_cache(cache_s, key);
    char *res = NULL;
    lock_reader();
    if (cache_s->cache[pos].t != 0)
        res = cache_s->cache[pos].value;
    unlock_reader();
    return res;
}

static inline void group_copy(GroupNode *group, struct Key *key, char *value, size_t value_size)
{

    memcpy(&group->key, key, sizeof(struct Key));
    group->t = clock();
    memcpy(group->value, value, value_size);
    group->size = value_size;
}

int write_cache(Cache_Set *cache_s, struct Key *key, char *value)
{
    size_t value_size = strlen(value) + 1;

    lock_reader();
    size_t cnt = cache_s->size;
    unlock_reader();
    int pos = find_cache(cache_s, key);
    if (value_size > MAX_OBJECT_SIZE)
        return -1;
    lock_writer();

    // no eviction
    if (cache_s->cache[pos].t == 0)
    {
        if (value_size + cnt <= MAX_CACHE_SIZE)
        {
            cache_s->cache[pos].value = (char *)malloc(sizeof(char) * value_size);
            group_copy(&cache_s->cache[pos], key, value, value_size);
            cache_s->size += value_size;
        }
        // eviction
        else
        {
            size_t min_t_pos = 0;
            Cache cache = cache_s->cache;
            int i = 0;
            for (; cache[i].t; ++i)
                if (cache[i].t < cache[min_t_pos].t)
                    min_t_pos = i;

            group_copy(&cache_s->cache[min_t_pos], key, value, value_size);
        }
    }
    else
    {
        cache_s->cache[pos].t = clock();
    }
    unlock_writer();
    return 0;
}