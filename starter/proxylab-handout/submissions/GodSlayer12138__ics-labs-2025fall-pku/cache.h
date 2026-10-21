#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_OBJS_COUNT 10

typedef struct {
    char cache_url[MAXLINE];
    char cache_data[MAX_OBJECT_SIZE];
    int cache_size;
    int lru_time;
    int is_empty;
} Cache_block;

void cache_init();
int read_cache(char *url, int fd);
void write_cache(char *url, char *data, int len);
