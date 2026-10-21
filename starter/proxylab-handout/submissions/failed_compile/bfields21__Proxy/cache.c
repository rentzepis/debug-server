#include <stdlib.h>
#include "csapp.h"
#include "cache.h"

// cache items are kept in a doubly linked list
typedef struct cacheList cacheList;
struct cacheList
{
   cacheItem *item; // cacheItem is defined in cache.h
   cacheList *prev;
   cacheList *next;
};

// The keyword static here makes these global variables
// and functions inaccessible outside of this file.

// pointer to the first node in the list
static cacheList *first;

// pointer to the last node in the list
static cacheList *last;

// sum of the sizes of all of the items in the list
static int currSize;

// functions that are only accessible to functions
// in this file
static void evict();
static void moveToFront(cacheList *ptr);

// cacheInit
// Initialize first and last to NULL and currSize to 0.
void cacheInit()
{
   // TODO
   first = NULL;
   last = NULL;
   currSize = 0;
   return;
}

// findCacheItem
// Search through the cacheList to try to find
// the item with the provided url.
// If found, move the node containing the cacheItem
// to the front of the list (using moveToFront)
// and return the pointer to the cacheItem.
// If not found, return NULL.
cacheItem *findCacheItem(char *url)
{
   // TODO
   // You'll need a cacheList * variable to iterate through
   // the linked list of cache objects.
   //
   cacheList *ptr;
   ptr = first;

   // Use strcmp to see if the url stored with the cacheItem
   // object matches the url passed into the function. (See
   // definition of cacheItem in cache.h.)
   //

   //if list is empty
   if (ptr == NULL)
   {
      return NULL;
   }

   while (ptr != NULL)
   {
      //if url match ptr url
      if (!strcmp(ptr->item->url, url))
      {
         moveToFront(ptr);
         return ptr->item;
      }
      ptr = ptr->next;
   }
   // If you find a match, call moveToFront to move
   // the cacheItem object to the front of the list and
   // return a pointer to it.
   //
   return NULL;
}

// moveToFront
// Move the node pointed to by cacheList to the
// front of the cacheList. This is used to
// implement the LRU policy.  The most recently
// used cache item is in the front of the list.
// The least recently used is at the end.
void moveToFront(cacheList *ptr)
{
   // TODO
   // You have three cases that you need to handle
   // a) the cacheList object is already in the front of the list
   //    i.e., ptr == first
   if (ptr == first)
   {
      return;
   }
   // b) the cacheList object is the last one in the list. In this
   //    case you'll need to update last and the prev field of the new
   //    last object (as well as first and the new first object)
   //
   if (ptr == last)
   {
      last = last->prev;
      last->next = NULL;
      first->prev = ptr;
      ptr->next = first;
      ptr->prev = NULL;
      first = ptr;
      return;
   }
   // c) not a) or b) (Drawing pictures helps!)
   cacheList *currPos = first;
   while (currPos != last)
   {
      if (ptr == currPos)
      {
         // found
         (ptr->prev)->next = ptr->next;
         (ptr->next)->prev = ptr->prev;
         first->prev = ptr;
         ptr->next = first;
         ptr->prev = NULL;
         first = ptr;
         return;
      }
      currPos = currPos->next;
   }

   return;
}

// addCacheItem
// This function takes a new item to add to the cache.
// You will need to dynamically allocate a cacheItem object
// and a cacheList object and initialize the fields of
// those objects. first will need to be modified to point
// to the new cacheList object.  last will need to be modified
// if it is currently NULL.
// If the adding the item to the cache would cause the
// size of the cache to exceed MAX_CACHE_SIZE, evict is
// called to evict one or more items from the cache to make
// room.
void addCacheItem(char *url, char *content, int size)
{
   // TODO
   // First, dynamically allocate a new cacheItem object
   // and initialize its content, size, and url fields
   //(See definition of cacheItem type in cache.h)
   //
   cacheItem *cacheItemObject;
   cacheItemObject = malloc(sizeof *cacheItemObject);

   cacheItemObject->content = content;
   cacheItemObject->size = size;

   int i;
   
   for (i = 0; i < MAXLINE - 1; i++)
   {
      if (url[i] == '\0')
      {
         cacheItemObject->url[i] = '\0';
         break;
      }
      cacheItemObject->url[i] = url[i];
   }

   // Second, dynamically allocate a new cacheList object.
   // It's item field will be set to the pointer to the new
   // cacheItem object.
   //
   cacheList *cacheListObject;
   cacheListObject = malloc(sizeof *cacheListObject);
   cacheListObject->item = cacheItemObject;

   // Third, insert the new cacheList object into the linked list.
   // You have two cases to handle:
   // a) first is NULL
   // b) first isn't NULL
   //
   cacheListObject->next = first;
   if (first == NULL)
   {
      last = cacheListObject;
   }
   else // if first isnt NULL
   {
      first->prev = cacheListObject;
   }
   cacheListObject->prev = NULL;
   first = cacheListObject;

   // Fourth if the new currSize is bigger than MAX_CACHE_SIZE
   // call evict to evict enough objects so that currSize <= MAX_CACHE_SIZE

   int newSize = size + currSize;
   if (newSize > MAX_CACHE_SIZE)
   {
      evict();
   }
   currSize = currSize + size;
   if (currSize >= MAX_CACHE_SIZE)
   {
      evict();
   }
   return;
}

// evict
// This function is called in order to evict items from the cache.
// In order to the implement the LRU policy, nodes are deleted from
// the end of the list until currSize is <= MAX_CACHE_SIZE
void evict()
{
   // TODO
   // Loop starting with the last cacheList object and iterating
   // toward the first object. Delete the object pointed to
   // by last and set last to the new last cacheList object. Adjust
   // currSize each time a cacheList object is deleted.  Repeat
   // while currSize is greater than MAX_CACHE_SIZE:
   cacheList *ptr;
   while (currSize > MAX_CACHE_SIZE)
   {
      ptr = last;
      last = last->prev;
      last->next = NULL;
      currSize = currSize - ptr->item->size;

      free(ptr); //delete what was the last block
   }

   return;
}

// useful for debugging
// Prints the list (url and size) in forward and backward direction
// to make sure the prev and next pointers are both correct.
void printCacheList()
{
   cacheList *iter;
   printf("printing forwards\n");
   for (iter = first; iter != NULL; iter = iter->next)
   {
      printf("url: %s, size: %d\n", iter->item->url, iter->item->size);
   }

   printf("printing backwards\n");
   for (iter = last; iter != NULL; iter = iter->prev)
   {
      printf("url: %s, size: %d\n", iter->item->url, iter->item->size);
   }
   return;
}
