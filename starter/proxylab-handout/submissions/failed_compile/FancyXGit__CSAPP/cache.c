#include "cache.h"

// 全局桶数组
static block_list *buckets[MAX_BUCKET_NUM];
// 全局LRU双向链表
static block_list *lru_list;
// LRU尾部
static block_list *lru_tail;
// 全局LRU锁
static pthread_mutex_t lru_mutex;
// 每一个桶的读写锁
static pthread_rwlock_t bucket_locks[MAX_BUCKET_NUM];
// 缓存已占用大小
static size_t total;

// 初始化缓存，仅允许全程序调用一次
void CacheInit(void)
{
    for (int i = 0; i < MAX_BUCKET_NUM; i++)
    {
        buckets[i] = NULL;
        pthread_rwlock_init(&bucket_locks[i], NULL);
    }
    lru_list = NULL;
    lru_tail = NULL;
    pthread_mutex_init(&lru_mutex, NULL);
    total = 0;
}

/*
 * 得到URL的哈希值
 * 使用普通的djb2
 */
static int HashURL(const char *url)
{
    unsigned long hash = 5381;
    int c;
    while ((c = *url++))
    {
        hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }
    return hash % MAX_BUCKET_NUM;
}

/*
 * 将lru_list中的一个node放到lru_list的头部
 */
static void setLRU(block_list *node)
{
    // 已经是链表头，跳过
    if (node->prev == NULL)
    {
        return;
    }

    block_list *prev_node = node->prev;
    block_list *next_node = node->next;

    // 处理是链表尾
    if (next_node == NULL)
    {
        prev_node->next = NULL;
        lru_tail = prev_node;
    }
    else
    {
        prev_node->next = next_node;
        next_node->prev = prev_node;
    }

    // 当前节点置于链表头
    block_list *head = lru_list;
    head->prev = node;
    node->next = head;
    node->prev = NULL;
    lru_list = node;
}

/*
 * 在LRU链表头插入新的节点
 */
static void addLRU(block_list *node)
{
    // 空链表
    if (lru_list == NULL)
    {
        lru_list = node;
        node->prev = NULL;
        node->next = NULL;
        lru_tail = node;
        return;
    }
    block_list *head = lru_list;
    head->prev = node;
    node->next = head;
    node->prev = NULL;
    lru_list = node;
}

/*
 * 在桶链表头中插入新的节点
 */
static void addNode(int bucket_index, block_list *node)
{
    // 对应桶为空
    block_list *head = buckets[bucket_index];
    if (head == NULL)
    {
        node->prev = NULL;
        node->next = NULL;
        buckets[bucket_index] = node;
        return;
    }
    // 桶不为空
    head->prev = node;
    node->next = head;
    node->prev = NULL;
    buckets[bucket_index] = node;
}

/*
 * 删除LRU链表尾部节点，返回尾部节点指针
 * 仅从链表删除，不释放内存
 */
static block_list *delLRUtail(void)
{
    // 空链表
    if (lru_tail == NULL)
    {
        return NULL;
    }
    // 只有一个元素
    if (lru_list == lru_tail)
    {
        block_list *node = lru_list;
        lru_list = NULL;
        lru_tail = NULL;
        return node;
    }
    block_list *node = lru_tail;
    block_list *prev_tail = lru_tail->prev;
    prev_tail->next = NULL;
    lru_tail = prev_tail;
    return node;
}

/*
 * 删除桶链表中的一个节点
 * 不会释放内存
 */
static block_list *delNode(int bucket_index, const char *url)
{
    block_list *curr_node = buckets[bucket_index];
    while (curr_node != NULL)
    {
        if (strcmp(curr_node->block->url, url) == 0)
        {
            break;
        }
        curr_node = curr_node->next;
    }
    if (curr_node == NULL)
    {
        return NULL;
    }
    block_list *prev_node = curr_node->prev;
    block_list *next_node = curr_node->next;
    if (prev_node == NULL)
    {
        // 当前节点为头
        if (next_node != NULL)
        {
            // 有下一个节点
            buckets[bucket_index] = next_node;
            next_node->prev = NULL;
            return curr_node;
        }
        else
        {
            // 没有下一个节点
            buckets[bucket_index] = NULL;
            return curr_node;
        }
    }
    else
    {
        // 当前节点不是头
        if (next_node == NULL)
        {
            // 当前节点是尾
            prev_node->next = NULL;
            return curr_node;
        }
        else
        {
            // 当前节点既不是头也不是尾
            prev_node->next = next_node;
            next_node->prev = prev_node;
            return curr_node;
        }
    }
}

/*
 * 根据URL在缓存中查找对应数据
 * 找到时返回0，将data设置为指向对应数据，len修改为数据的长度
 * 未找到返回1
 * 出错返回-1
 */
int getCache(const char *url, char **data, size_t *len)
{
    char *buf;
    // 计算索引
    int bucket_index = HashURL(url);
    // 对桶加读锁
    pthread_rwlock_rdlock(&bucket_locks[bucket_index]);
    // 遍历桶对应链表
    block_list *curr_node = buckets[bucket_index];
    while (curr_node != NULL)
    {
        cache_block *curr_block = curr_node->block;
        // 匹配
        if (strcmp(url, curr_block->url) == 0)
        {
            // 对LRU加互斥锁
            pthread_mutex_lock(&lru_mutex);
            // 更新lru链表
            setLRU(curr_node);
            // 释放互斥锁
            pthread_mutex_unlock(&lru_mutex);

            // 创建缓存区并拷贝数据
            size_t size = curr_block->data_len;
            buf = (char *)malloc(sizeof(char) * size);
            if (buf == NULL)
            {
                return -1;
            }
            memcpy(buf, curr_block->data, size);
            *data = buf;
            *len = size;

            // 释放读锁并返回
            pthread_rwlock_unlock(&bucket_locks[bucket_index]);
            return 0;
        }
        curr_node = curr_node->next;
    }
    // 未匹配
    pthread_rwlock_unlock(&bucket_locks[bucket_index]);
    return 1;
}

/*
 * 释放缓存中最旧的一项
 * 成功返回0
 * 失败返回1
 */
int freeCache(void)
{
    char buf[MAX_URL_LENGTH];
    // 对LRU加互斥锁
    pthread_mutex_lock(&lru_mutex);
    // 找尾部候选块记录
    block_list *end_node = lru_tail;
    if (end_node == NULL)
    {
        return;
    }
    strcpy(buf, end_node->block->url);
    // 释放LRU互斥锁
    pthread_mutex_unlock(&lru_mutex);

    // 找到对应桶加锁
    int bucket_index = HashURL(buf);
    pthread_rwlock_wrlock(&bucket_locks[bucket_index]);
    // 加LRU互斥锁
    pthread_mutex_lock(&lru_mutex);
    // 检查是否在尾部
    if (strcmp(lru_tail->block->url, buf) == 0)
    {
        // URL相同，在尾部
        // 释放内存
        // 1. 释放LRU链表节点
        block_list *curr_lru;
        if ((curr_lru = delLRUtail()) != NULL)
        {
            free(curr_lru);
        }
        // 2. 释放桶链表节点
        // 找到桶
        cache_block *curr_block = NULL;
        block_list *curr_node = delNode(bucket_index, buf);
            if (curr_node != NULL)
        {
            curr_block = curr_node->block;
            free(curr_node);
        }
        // 3. 释放数据空间
        if (curr_block == NULL)
        {
            return 1;
        }
        total -= curr_block->data_len;
        free(curr_block->url);
        free(curr_block->data);
        // 4. 释放缓存块结构空间
        free(curr_block);
        

        // 释放互斥锁
        pthread_mutex_unlock(&lru_mutex);
        // 释放读锁并返回
        pthread_rwlock_unlock(&bucket_locks[bucket_index]);
        return 0;
    }
    else
    {
        // 释放互斥锁
        pthread_mutex_unlock(&lru_mutex);
        // 释放读锁并返回
        pthread_rwlock_unlock(&bucket_locks[bucket_index]);
        return 1;
    }
}

void writeCache(const char *url, const char *data, size_t len)
{
    // 抛弃过大数据
    if (len > MAX_OBJECT_SIZE)
    {
        return;
    }

    // 计算索引
    int bucket_index = HashURL(url);

    while (1)
    {
        // 对对应桶加写锁
        pthread_rwlock_wrlock(&bucket_locks[bucket_index]);
        // 对LRU加锁
        pthread_mutex_lock(&lru_mutex);

        // 查询是否有对应URL
        int contain_url = 0;
        block_list *curr_node = buckets[bucket_index];
        while (curr_node != NULL)
        {
            cache_block *curr_block = curr_node->block;
            // 匹配
            if (strcmp(url, curr_block->url) == 0)
            {
                contain_url = 1;
                break;
            }
            curr_node = curr_node->next;
        }

        // 有URL，缓存命中
        if (contain_url == 1)
        {
            // 更新LRU
            setLRU(curr_node);
            // 释放互斥锁
            pthread_mutex_unlock(&lru_mutex);
            // 释放读锁并返回
            pthread_rwlock_unlock(&bucket_locks[bucket_index]);
            return;
        }

        // 无URL
        // 判断缓存是否足够
        if (total < MAX_CACHE_SIZE)
        {
            // 分配堆空间写入数据，并设置对应链表
            // 1. 分配数据空间
            // 数据
            char *curr_data = malloc(sizeof(char) * len);
            if (curr_data == NULL)
            {
                return;
            }
            memcpy(curr_data, data, len);
            // URL
            int url_len = strlen(url);
            char *curr_url = malloc(sizeof(char) * (url_len + 1));
            if (curr_url == NULL)
            {
                return;
            }
            memcpy(curr_url, url, url_len);
            curr_url[url_len] = '\0';

            // 2. 分配桶链表节点空间，并设置
            // 缓存块
            cache_block *curr_block = malloc(sizeof(cache_block));
            if (curr_block == NULL)
            {
                return;
            }
            curr_block->data = curr_data;
            curr_block->url = curr_url;
            curr_block->data_len = len;
            // 桶链表节点
            block_list *curr_node = malloc(sizeof(block_list));
            if (curr_node == NULL)
            {
                return;
            }
            curr_node->block = curr_block;
            addNode(bucket_index, curr_node);

            // 3. 分配LRU链表节点空间，并设置
            block_list *curr_lru = malloc(sizeof(block_list));
            if (curr_lru == NULL)
            {
                return;
            }
            curr_lru->block = curr_block;
            addLRU(curr_lru);

            // 记录缓存添加大小
            total += len;

            // 释放锁，返回
            // 释放互斥锁
            pthread_mutex_unlock(&lru_mutex);
            // 释放读锁并返回
            pthread_rwlock_unlock(&bucket_locks[bucket_index]);
            return;
        }
        else
        {
            // 释放互斥锁
            pthread_mutex_unlock(&lru_mutex);
            // 释放读锁并返回
            pthread_rwlock_unlock(&bucket_locks[bucket_index]);
            // 循环尝试释放缓存
            while (freeCache() != 0)
            {
            }
        }
    }
}