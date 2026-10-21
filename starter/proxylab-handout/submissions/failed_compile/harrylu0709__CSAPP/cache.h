#include "csapp.h"
#define MAX_CACHE_SIZE  1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_LINE_NUM  10
typedef struct cache_line
{
    char cache_object[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int valid;
    int size;
    int timestamp;
}Cache_line_t;


typedef struct cache
{
    Cache_line_t cache_line[CACHE_LINE_NUM];
    int readcnt, currentTime, totalSize;
    sem_t readcnt_mutex, writer_mutex; 
}Cache_t;

Cache_t cache;

void cache_init(void);
int cache_read(char *uri, int fd);
void cache_write(char *server_res, char* uri, int size);