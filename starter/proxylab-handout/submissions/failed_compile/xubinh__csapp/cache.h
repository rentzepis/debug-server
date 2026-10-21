#include <semaphore.h>
#include <stddef.h>

/**
 * @brief 对单个缓存对象的抽象
 *
 */
typedef struct CacheNode {
    char *key;
    void *value;
    size_t value_size;   ///< 整个对象的大小 (包括元数据和有效载荷)
    size_t payload_size; ///< 有效载荷的大小 (只有载荷大小被用于计算空间占用, 元数据不作考虑)
    struct CacheNode *previous_node;
    struct CacheNode *next_node;
} CacheNode;

/**
 * @brief 对整个缓存的抽象
 *
 */
typedef struct {
    size_t segment_number;                               ///< 缓存分段的数量
    size_t max_segment_size;                             ///< 单个缓存段的最大总载荷大小
    size_t max_payload_size;                             ///< 最大有效载荷大小
    size_t *current_segment_sizes;                       ///< 缓存段的当前总载荷大小
    CacheNode **segments;                                ///< 缓存段本身 (抽象为缓存对象的双向链表)
    int *reader_counts;                                  ///< 每个缓存段当前的读者数量
    sem_t *reader_semaphores;                            ///< 每个缓存段的读者互斥锁
    sem_t *readers_writers_mutexes;                      ///< 每个缓存段的读者-写者互斥锁
    int *read_only_reader_counts;                        ///< 每个缓存段当前的只读读者数量
    sem_t *read_only_reader_semaphores;                  ///< 每个缓存段的只读读者互斥锁
    sem_t *read_only_readers_try_modify_readers_mutexes; ///< 每个缓存段的只读读者-欲修改读者互斥锁
} Cache;

/**
 * @brief 初始化缓存
 *
 * @param cache 缓存
 * @param segment_number 要设置的缓存分段数量
 * @param max_cache_size 要设置的最大缓存大小 (将会被等分为单个缓存段的最大总载荷大小)
 * @param max_payload_size 要设置的最大有效载荷大小
 */
void cache_initialize(Cache *cache, size_t segment_number, size_t max_cache_size, size_t max_payload_size);

/**
 * @brief 释放缓存
 *
 * @param cache 缓存
 */
void cache_free(Cache *cache);

/**
 * @brief 向缓存中存放对象
 *
 * @param cache 缓存
 * @param key 键
 * @param key_size 键的长度
 * @param new_value 对象
 * @param new_value_size 对象的大小
 * @param new_payload_size 对象的有效载荷大小
 */
void cache_put(Cache *cache, char *key, size_t key_size, void *new_value, size_t new_value_size, size_t new_payload_size);

/**
 * @brief 从缓存中获取对象
 *
 * @param cache 缓存
 * @param key 键
 * @param destination 返回的缓冲区
 * @param value_size 对象大小
 * @return int 指示是否成功获取到对象
 */
int cache_get(Cache *cache, char *key, void **destination, size_t *value_size);