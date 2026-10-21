#ifndef CACHE_H
#define CACHE_H

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_CACHE 10

typedef struct
{
    char obj[MAX_OBJECT_SIZE];
    char uri[MAXLINE];
    int lru_cnt;
    int isEmpty;
    int size;

} Block;

typedef struct
{
    Block data[MAX_CACHE];
    int num;
} Cache;

void cache_init();
int cache_find(char *url, char *buf, int *size);
void cache_add(char *url, char *buf, int size);

#endif
