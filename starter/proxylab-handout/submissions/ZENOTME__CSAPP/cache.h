#ifndef _CACHE_
#define _CACHE_

typedef struct cache_node_t{
	char *name;
	char *buf;
	int length;
	struct cache_node_t* next;
	struct cache_node_t* last;
}cache_node_t;

typedef struct{
	int sum_length;
	int n;
	int read_cnt;
	sem_t s_mux;
	sem_t s_write;
	struct cache_node_t* start;
	struct cache_node_t* end;
}cache_t;

void cache_init(cache_t* cache);
char* cache_read(cache_t* cache,char *url);
void cache_write(cache_t *cache,char *buf,char *url,int len);
#endif
