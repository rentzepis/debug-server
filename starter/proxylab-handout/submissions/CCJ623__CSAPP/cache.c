#include "cache.h"
#include "csapp.h"

/* CacheEntry begin */

CacheEntry* createCacheEntry(const char* url, const void* data, size_t data_length)
{
    if (data_length > MAX_OBJECT_SIZE)
    {
        app_error("Data length is bigger than 100Kib!");
    }
    CacheEntry* result = malloc(sizeof(CacheEntry));
    size_t url_length = strlen(url) + 1;
    size_t total_size = url_length + data_length;
    result->url = (char*)malloc(total_size);
    result->data = result->url + url_length;
    result->data_length = data_length;
    result->last_accessed = 0;
    result->prev = result->next = NULL;
    memcpy(result->url, url, url_length);
    memcpy(result->data, data, data_length);
    return result;
}

void freeCacheEntry(CacheEntry* entry)
{
    free(entry->url);
    free(entry);
}


/* CacheEntry end */

/* Cache begin */

void initCache(Cache* cache)
{
    memset(cache->hash_table, 0, sizeof(cache->hash_table));
    cache->head = cache->tail = NULL;
    cache->used_size = 0;
}

void freeCache(Cache* cache)
{
    CacheEntry* entry = cache->head,
        * next_entry = entry->next;
    while (entry != NULL)
    {
        freeCacheEntry(entry);
        entry = next_entry;
        next_entry = entry->next;
    }
}

void removeEntry(Cache* cache, CacheEntry* entry)
{
    CacheEntry* prev = entry->prev,
        * next = entry->next;
    if (prev != NULL)
    {
        prev->next = entry->next;
    }
    if (next != NULL)
    {
        next->prev = prev;
    }
    cache->used_size -= entry->data_length;
    freeCacheEntry(entry);
}

void evictEntry(Cache* cache)
{
    cache->tail = cache->tail->prev;
    removeEntry(cache, cache->tail->next);
}

void makeSpace(Cache* cache, size_t target_space)
{
    if (target_space > MAX_CACHE_SIZE)
    {
        app_error("Can not make space more than 1Mib");
    }

    while (MAX_CACHE_SIZE - cache->used_size < target_space)
    {
        evictEntry(cache);
    }
}

/**
 * @brief 计算给定字符串的BKDR哈希值。
 *
 * BKDR哈希算法是一种简单而有效的字符串哈希函数。
 * 它的核心思想是使用一个常数（例如31、131、1313等）与当前哈希值进行乘法操作，
 * 并加上当前字符的ASCII值，然后将结果进行位移。
 * 这种连续的乘法和加法操作能够有效地分散字符串中字符的影响，
 * 使得不同的字符串产生不同的哈希值，从而减少哈希冲突的概率。
 * 这里的乘数（也称为“魔法数”）通常是一个素数，以获得更好的散列效果。
 *
 * @param str 指向要计算哈希值的C字符串（以空字符结束）。
 * @return unsigned int 返回计算出的哈希值。
 */
unsigned int hash(const char* str)
{
    // 定义哈希算法的乘数，通常选择一个素数以获得更好的散列效果。
    // 这里使用31，也可以选择131、1313等。
    // 这个常数是BKDR算法的关键参数，它影响了哈希值的生成过程。
    unsigned int seed = 31;
    // 初始化哈希值为0。哈希值将在这个变量中累积。
    unsigned int hash = 0;

    // 遍历字符串中的每一个字符，直到遇到空字符（字符串结束符）。
    while (*str)
    {
        // 哈希计算的核心步骤：
        // 1. 将当前哈希值乘以seed。这会将之前字符的影响“移动”到更高的位。
        // 2. 加上当前字符的ASCII值。这引入了新字符的影响。
        // 通过这种方式，每个字符都会以其位置和值的方式贡献到最终的哈希值中。
        hash = hash * seed + (*str++);
    }

    // 返回计算出的最终哈希值。
    return hash;
}

void appendEntry(Cache* cache, CacheEntry* entry)
{
    makeSpace(cache, entry->data_length);
    entry->prev = NULL;
    entry->next = cache->head;
    entry->last_accessed = time(NULL);
    cache->head = entry;
    cache->used_size += entry->data_length;
    size_t index = hash(entry->url) % HASH_TABLE_SIZE;
    CacheEntry* curr_entry = cache->hash_table[index];
    entry->hash_table_next = curr_entry;
    cache->hash_table[index] = entry;
}

void moveEntryToFront(Cache* cache, CacheEntry* entry)
{
    CacheEntry* prev = entry->prev,
        * next = entry->next;
    if (prev != NULL)
    {
        prev->next = entry->next;
    }
    if (next != NULL)
    {
        next->prev = prev;
    }
    entry->prev = NULL;
    entry->next = cache->head;
    cache->head = entry;
    entry->last_accessed = time(NULL);
}

CacheEntry* readEntry(Cache* cache, const char* url)
{
    size_t index = hash(url) % HASH_TABLE_SIZE;
    CacheEntry* result = cache->hash_table[index];
    for (; result != NULL && strcmp(url, result->url) != 0;
        result = result->hash_table_next);
    if (result != NULL)
    {
        moveEntryToFront(cache, result);
    }
    return result;
}

/* Cache end */