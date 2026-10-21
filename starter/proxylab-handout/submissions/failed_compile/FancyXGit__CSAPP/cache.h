#ifndef __CACHE_H_
#define __CACHE_H_

#include "csapp.h"
#define MAX_BUCKET_NUM 64
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_URL_LENGTH 128

/*
 * 缓存设计
 * 题目假设访问同一个网站的URL返回的内容都是不变的
 * 因此可以以URL为TAG设计全相联缓存
 *
 * 设计如下4种基本数据结构：
 * 1. 缓存块：缓存的基本单元。包括URL指针和DATA指针，分别指向堆中存储的数据
 * 2. 桶：指向一个双向缓存块链表，每一个缓存块链表中URL的哈希值相同
 * 3. 桶数组：全局桶的数组
 * 4. LRU双向链表：链表中元素是缓存块，按照新的排到链表头顺序排列，便于指向LRU策略
 *
 * 设计两种锁：
 * 1. 桶锁：对每个桶设置读锁和写锁，采用读者优先策略。读锁禁止写者写，写锁禁止读者读
 * 2. LRU锁：对LRU链表加互斥锁
 * 锁使用标准库锁pthread_mutex_t和pthread_rwlock_t，不使用CSAPP提供的P和V操作
 *
 * 全局规则：
 * 1. 任何线程如果需要同时持有桶锁和LRU锁，必须先获取桶锁，再获取LRU锁。单独获取LRU锁是允许的，但在持有LRU锁期间绝不允许再去获取任何桶锁。
 * 2. 返回数据时，创建缓冲区拷贝数据，返回缓冲区拷贝而不是堆中的地址
 *
 * 读数据：
 * 1. 输入URL，通过哈希函数计算得到索引
 * 2. 对桶加读锁，遍历对应索引桶的对应链表
 * 缓存命中:
 * 1. 对LRU加互斥锁，更新LRU链表，释放互斥锁
 * 2. 拷贝数据进缓冲区
 * 3. 释放读锁，返回命中
 * 缓存不命中：
 * 1. 释放读锁，返回不命中
 *
 *
 * 写数据：
 * 1. 检查数据大小
 * 数据过大：抛弃
 * 数据合理：
 * 1. 对对应的桶加写锁
 * 2. 对LRU加互斥锁
 * 3. 查询对应的桶是否有URL
 * 缓存命中：
 * 1. 更新LRU
 * 2. 释放互斥锁，释放写锁，返回
 * 缓存不命中：判断缓存是否足够
 * 缓存足够：
 * 1. 写入数据，更新LRU
 * 2. 释放LRU
 * 3. 释放写锁
 * 缓存不够
 * 1. 释放LRU，释放写锁
 * 2. 循环执行淘汰策略，成功再从判断缓存是否命中开始
 *
 * 淘汰策略：
 * 1. 对LRU加互斥锁，找到尾部缓存候选块，此时只记录，释放LRU互斥锁
 * 2. 找到候选块对应桶，加写锁
 * 3. 对LRU加互斥锁，检查是否在尾部
 * 4. 若在，释放内存，删除对应桶中节点和LRU链表节点释放桶写锁，释放LRU互斥锁，释放桶写锁,返回淘汰成功
 * 5. 不在，释放LRU互斥锁，释放桶写锁，返回淘汰失败
 */

typedef struct cache_block
{
    char *url;
    size_t data_len;
    char *data;
} cache_block;

typedef struct block_list
{
    cache_block *block;
    struct block_list *prev;
    struct block_list *next;
} block_list;

void CacheInit(void);
int getCache(const char *url, char **data, size_t *len);
int freeCache(void);
void writeCache(const char *url, const char *data, size_t len);

#endif
