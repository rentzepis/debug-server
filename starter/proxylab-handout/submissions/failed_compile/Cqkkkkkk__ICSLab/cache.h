
#include "csapp.h"
#include <sys/time.h>

#define CACHENUM 5
#define MAX_CACHE_SIZE 1049000

typedef struct cache_block {
	char* url;
	char* data;
	int size;
	long long time;
	sem_t mutex;
} cache_block;


long long GetCurTime();

//intialize cache with malloc
void init_cache();
//if miss cache return 0, hit cache write content to fd
int Read_Cache_Response(char* url, int fd);
//save value to cache
void write_cache(char* url, char* data, int len);
//free cache
void free_cache();

void Response_Cached(int endserver_fd, char* cached_data, int data_size);