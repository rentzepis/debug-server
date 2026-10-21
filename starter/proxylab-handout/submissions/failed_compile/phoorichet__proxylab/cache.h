//
//  cache.h
//  proxylabxcode
//
//  Created by Lock on 4/22/13.
//  Copyright (c) 2013 Lock. All rights reserved.
//

#ifndef proxylabxcode_cache_h
#define proxylabxcode_cache_h

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

struct cacheobject_t {
	struct cacheobject_t *prev, *next;
	size_t size;
	char *uri;
	void *data;
};
typedef struct cacheobject_t CacheObject;

typedef struct {
	CacheObject *head, *tail;
	size_t size;
	sem_t mutex, w;
	int readcnt;
} cache_t;

cache_t cache;

void cache_init();
CacheObject *cache_get(char *uri);
int cache_insert(char *uri, void *data, size_t objectsize);
void cache_evict();

void print_cache();
void check_cache();

#endif
