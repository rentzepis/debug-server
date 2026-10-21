/* Recommended max cache and object sizes */
#include "csapp.h"
#include <stdatomic.h>
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE_NUM (MAX_CACHE_SIZE/MAX_OBJECT_SIZE)
typedef struct cache_file_t{
    int file_size;
    int timestamp;
    FILE* file;
    char url[MAXLINE];
} cache_file_t;
typedef struct cache_t {
    int used_num;
    atomic_uint read_cnt;
    atomic_uint timestamp;
    sem_t writer_lock;
    cache_file_t files[MAX_CACHE_NUM];
}cache_t;

void cache_init();
int cache_query(const char* url, int client_fd);
void cache_add(const char* url, int server_fd, int client_fd);
void splice_forward(int src_fd, int dst_fd);