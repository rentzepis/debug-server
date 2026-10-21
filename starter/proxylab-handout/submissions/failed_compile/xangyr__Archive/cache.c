#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "csapp.h"
#include "cache.h"


/* cache_init initializes the input cache linked list. */
void cache_init(CacheList *list) {
  list->size = 0;
  list->first = NULL;
  list->last = NULL;
}

//implement free_cache for repeating free
void free_cache(CachedItem *cache) {
    free(cache->url);
    free(cache->headers);
    free(cache->item_p);
    free(cache);
}

/* cache_URL adds a new cached item to the linked list. It takes the
 * URL being cached, a link to the content, the size of the content, and
 * the linked list being used. It creates a struct holding the metadata
 * and adds it at the beginning of the linked list.
 */
void cache_URL(const char *URL, const char *headers, void *item, size_t size, CacheList *list) {
  //initialize all variables in cacheditem
  CachedItem *cache = malloc(MAX_OBJECT_SIZE);
  cache->url = strdup(URL);
  cache->headers = strdup(headers);
  cache->item_p = item;
  cache->size = size;
  cache->prev = NULL;
  cache->next = NULL;
  
  //if cacheditem size is bigger than max, free, not cache
  if (size > MAX_OBJECT_SIZE) {
    free_cache(cache);
    return;
  }
  
  //if size is not enough, delete nodes untile enough space to cache
  while ((list->size + size) > MAX_CACHE_SIZE) {
    CachedItem *current = list->last;
    current->prev->next = NULL;
    list->last = current->prev;
    list->size -= current->size;
    free_cache(current);
  }
  //if has cached something in list, update to the first, move others
  if (list->size) {
    cache->next = list->first;
    list->first->prev = cache;
    list->first = cache;
    
  } 
  //if empty, put in, update first and last
  else {
    list->first = cache;
    list->last = cache;
  }
  //update list size
  list->size += size;
  return;
}


/* find iterates through the linked list and returns a pointer to the
 * struct associated with the requested URL. If the requested URL is
 * not cached, it returns null.
 */
CachedItem *find(const char *URL, CacheList *list) {
  //only find when has cache in list
  if (list->size) {
    CachedItem *current = list->first;

    while(current) {
      //found
      if (!strcasecmp(current->url, URL)) {
        //is the only cache in list
        if (list->size == current->size) {}
          //return current;
        /*
          is the last cache in list
          move to first, update first and last
        */
        else if (current == list->last) {
          current->next = list->first;
          list->first->prev = current;
          list->first = current;
          list->last = current->prev;
          current->prev->next = NULL;
          current->prev = NULL;
        }
        //in the middle of list, move to first, update first
        else {
          current->prev->next = current->next;
          current->next->prev = current->prev;
          current->prev = NULL;
          current->next = list->first;
          list->first = current;
        }
        return current;
      }
      current = current->next;
    }
  }
  return NULL;
}


/* frees the memory used to store each cached object, and frees the struct
 * used to store its metadata. */
void cache_destruct(CacheList *list) {
  CachedItem *current = list->first;
  //loop through, free every cache, move to next, then init whole list
  while (current) {
    CachedItem *temp = current->next;
    free_cache(current);
    current = temp;
  }
  cache_init(list);
  return;
}
