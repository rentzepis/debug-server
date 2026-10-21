#include <stdio.h>
#include <stdlib.h>
#include <stdio.h>
#include "csapp.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CachedData CachedData;
typedef struct Cachelist Cachelist;

struct Cachelist
{
    CachedData *head;
};

struct CachedData
{
    char *c_key[MAXBUF];
    char *c_val[MAX_OBJECT_SIZE];
    CachedData *next;
};

void insertcache(Cachelist *list, CachedData *my_data)
{
    CachedData *p = list->head;
    list->head = my_data;
    list->head->next = p;
    return;
};

CachedData *findcache(Cachelist *list, char *filename)
{
    CachedData *cur_cache = list->head;

    while (cur_cache != NULL)
    {
        if (!strcmp(cur_cache->c_key, filename))
            return cur_cache;

        else
            cur_cache = cur_cache->next;
    }

    return NULL;
};