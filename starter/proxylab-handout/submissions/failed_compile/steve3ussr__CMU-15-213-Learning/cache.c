#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include "csapp.h"
#include "cache.h"


int hash(char *url, int base){
    int res = 0;
    int n = strlen(url);
    for (int i=0; i<n; i++){
        res ^= (url[i] + (url[i]>>4)) & (base-1);
    }
    return res;
}

void cache_init(cache_t *cp, int entry_num, int cache_size, int entry_size){

    cp->length = entry_num;
    cp->max_cache_size = cache_size;
    cp->curr_cache_size = 0;
    cp->max_entry_size = entry_size;
    sem_init(&cp->lock, 0, 1);

    cp->entries = calloc(entry_num, sizeof(entry_t *));

    entry_t *ep;
    for (int i = 0; i < entry_num; i++){
        cp->entries[i] = ep = (entry_t *)malloc(sizeof(entry_t));

        ep->active = 0;
        ep->key = NULL;
        ep->value_length = 0;
        ep->value = NULL;
    }
}

void cache_destruct(cache_t *cp){
    entry_t *ep;

    for (int i = 0; i < cp->length; i++){
        ep = cp->entries[i];
        if (ep->active){
            free(ep->key);
            free(ep->value);}

        free(cp->entries[i]);
    }
    free(cp->entries);
}

void cache_show(cache_t *cp)
{
    entry_t *ep;
    char *tmp;
    printf("index   mem_location   active   key          value-length\t value\n");
    for (int i=0; i<cp->length; i++)
    {
        ep = cp->entries[i];
        tmp = malloc(ep->value_length+1);
        memset(tmp, 0, ep->value_length+1);
        memcpy(tmp, ep->value, ep->value_length);
        printf("  %-3d %p   %-4d   %-15s      %-6d\t \n", i, ep, ep->active, ep->key, ep->value_length);
        free(tmp);
    }
}

int cache_search(cache_t *cp, char *url){
    int index = hash(url, cp->length);
    entry_t *ep = cp->entries[index];
    if (ep->active == 0) return -1;            // -1: not used
    if (strcmp(ep->key, url) != 0) return -2;  // -2: miss
    return index;                              //  i: HIT!
}

void cache_evict(cache_t *cp, char *url){
    int index = hash(url, cp->length);
    entry_t *ep = cp->entries[index];
    ep->active = 0;
    cp->curr_cache_size -= ep->value_length;
    free(ep->key);
    free(ep->value);
    ep->value_length = 0;
}

void cache_add(cache_t *cp, char *url, void *mem_src, int length){
    int res_hit = cache_search(cp, url);  // -1: not used    -2: miss    i: HIT!
    if (res_hit >= 0) {return;}
    if (res_hit == -2) {cache_evict(cp, url);}

    int index = hash(url, cp->length);
    entry_t *ep = cp->entries[index];

    ep->active = 1;
    cp->curr_cache_size += length;
    ep->value_length = length;

    // ep->key
    int url_length = strlen(url);
    ep->key = malloc(url_length+1);
    memset(ep->key, 0, url_length+1);
    memcpy(ep->key, url, url_length);

    // ep->value
    ep->value_length = length;
    ep->value = (char *)malloc(length);
    memcpy(ep->value, mem_src, length);
}

void cache_acquire_lock(cache_t *cp) {P(&cp->lock);}
void cache_release_lock(cache_t *cp) {V(&cp->lock);}