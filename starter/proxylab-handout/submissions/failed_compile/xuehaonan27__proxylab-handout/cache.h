#ifndef CACHE_H
#define CACHE_H
/* 这个27的目的是过那个cache-lru */
#define CACHE_SLOT 27
#include <stdlib.h>
#include <string.h>
// cache_object_t Cache[CACHE_SLOT];
// unsigned int global_time;
/* 在缓存里面存储的条目, 是否缓存由响应的Content-Length字段决定 */
typedef struct {
    unsigned int id; /* 由哈希生成的序列号, 标识一个uri */
    unsigned int timestamp; /* 时间戳, 用于实现LRU策略 */
    char *objp; /* 指向一个HTTP相应字符串的指针 */
    size_t obj_len; /* obj_len存储的是objp字符串的长度, 为了防止strlen在响应中存在二进制数据时失效而加上的一个而外的字段 */
} cache_object_t;

/* 初始化全局缓存 */
void init_cache(cache_object_t *Cache, size_t cache_size);

/* 根据id在全局缓存中尝试寻找项 */
cache_object_t *find_id(cache_object_t *Cache, size_t cache_size, unsigned int id);

/* 向全局缓存中存储项, 可能发生驱逐 */
void store_obj(cache_object_t *Cache, size_t cache_size, cache_object_t *obj);
#endif