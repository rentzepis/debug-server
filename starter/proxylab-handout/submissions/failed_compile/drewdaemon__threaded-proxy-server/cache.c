#include "cache.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_NUMBER 5

typedef struct cache_obj {
  char *url;
  int size;
  struct cache_obj *next;
  unsigned char *data;
} cache_obj ;

cache_obj *HEAD;
cache_obj *TAIL;
int cache_size = 0;
sem_t cache_sem;

void cache_init() {
  Sem_init(&cache_sem, 0, 10);
}

void pop_head() {
  if(!(HEAD->next)) {
    return;
  }
  cache_size -= HEAD->size;
  cache_obj *temp = HEAD;
  HEAD = HEAD->next;
  Free(temp);
}

void cache_object(char* url, unsigned char *data, int data_size) {
  if (data_size > MAX_OBJECT_SIZE) {
    printf("TOO BIG TO CACHE\n");
    return;
  }
  P(&cache_sem);
  if (HEAD) {
    TAIL->next = malloc(sizeof(cache_obj));
    TAIL = TAIL->next;
    TAIL->size = data_size;
    TAIL->data = data;
    TAIL->url = url;
  }
  else {
    HEAD = TAIL = malloc(sizeof(cache_obj));
    TAIL->size = data_size;
    TAIL->data = data;
    TAIL->url = url;
  }
  cache_size += data_size;
  if (cache_size > MAX_CACHE_SIZE) {
    pop_head();
  }
  V(&cache_sem);
}

void cache_print() {
  P(&cache_sem);
  cache_obj *search = HEAD;
  while (search) {
    printf("%s %d\n", search->url, search->size);
    search = search->next;
  }
  V(&cache_sem);
}

unsigned char *cache_get_object(char *url, int *size) {
  P(&cache_sem);
  cache_obj *search = HEAD;
  while (search) {
    if (!strcmp(search->url, url)) {
      *size = search->size;
      return search->data;
    }
    search = search->next;
  }
  V(&cache_sem);
  return NULL;
}
