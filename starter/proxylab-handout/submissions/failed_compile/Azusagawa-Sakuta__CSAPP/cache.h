#include <unistd.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct {
    char buf[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int length;
    int valid;
    int timestamp;
} CacheBlock;

typedef struct {
    CacheBlock cache_block[MAX_CACHE_SIZE/MAX_OBJECT_SIZE];
    int readcnt;
    sem_t mutex, w;
} Cache;

void cache_init();
int cache_read(char* uri, int fd);
void cache_write(char* msg2send, char* uri, ssize_t len);