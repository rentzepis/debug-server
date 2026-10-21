/*
 * ksikka, mchoquet
 */

#ifndef __CACHE_H__
#define __CACHE_H__

struct cache_key {
  char* hostName;
  int serverPort;
  char* filePath;
};
typedef struct cache_key* keyptr;

int initCache(int maxItemSize, int maxCacheSize);
void* lookup(keyptr k, int* len);
int update(keyptr k, void* v, int vSize);

#endif

