#include <semaphore.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NCACHESLOTS 16

typedef struct
{
    char* buf;          // cache buffer
    int size;           // cache size
    char* path;         // request path
    sem_t mutex;        // mutexg lock for read_cnt
    int read_cnt;       // read count

    time_t last_access; // last access time
    sem_t access;       // access lock
} CacheLine;

typedef struct
{
    int total_size;
    CacheLine cachebuf[NCACHESLOTS];
} Cache;

typedef struct
{
    char* buf;
    int offset;
} CacheBuff;


int read_cache(Cache*, char*, char*);
int write_cache(Cache*, char*, char*, int);
void init_cache(Cache*);
void Rio_writeBuf(int fd, CacheBuff* dest, char* src, int size, int* flag);
void init_cachebuf(CacheBuff*);
void deinit_cachebuf(CacheBuff*);
void print_cache(Cache*);
