
#ifndef CACHE_H
#define CACHE_H

#include <stdio.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


typedef struct {
    char* host;
    char* user_agent;
    char* type;
    char* connection;
    char* proxy_connection;
    char* path;
    char* portnumber;

} headers_t;

typedef struct obj{
    headers_t* heads;
    struct obj* next; // more recently used thant the obj
    struct obj* prev; // less recently used than the obj
    char* tosend;
    size_t size;
} obj_t;

obj_t* least_recently_used;
obj_t* most_recently_used;
size_t cache_memory_used;
sem_t mutex, s;
int number_of_obj;
int isreading ;


void cache_init();
void deletee(obj_t *obj);
void push(obj_t* obj);
void evict();
void myfree(headers_t* heads, obj_t* obj);
obj_t* create_obj(headers_t *heads, size_t size, char* tosend);
obj_t* find_in_cache(headers_t* heads);
int isEqualObj(headers_t* heads, obj_t* obj);
int isEqualHead(headers_t* heads, headers_t* heads2);

#endif