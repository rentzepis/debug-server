#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

void cache_init();
void cache_print();
void cache_object(char* path, unsigned char *data, int data_size);
unsigned char *cache_get_object(char *url, int *size);

#endif
