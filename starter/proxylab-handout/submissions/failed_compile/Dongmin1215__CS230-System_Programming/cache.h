#include <stdlib.h>
#include <string.h>

#define MAX_SIZE 1049000
#define MAX_OBJ_SIZE 102400

typedef struct object{
    char *key;
    char * data;
    int size;
    struct object* next;
    struct object* prev;
}object;


typedef struct cache{
    struct object* start;
    struct object* end;
    size_t size;
}cache;

cache* cache_new();
void cache_free(cache* c);
void cache_add(cache* c, char* key, char* data, int size);
void cache_update(cache*c, object* obj);
object* cache_lookup(cache* c, char* key);

