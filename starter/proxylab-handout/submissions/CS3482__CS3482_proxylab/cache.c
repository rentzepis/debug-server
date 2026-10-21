#include <stdlib.h>
#include "csapp.h"
#include "cache.h"

//cache items are kept in a doubly linked list
typedef struct cacheNodeT cacheNodeT;
struct cacheNodeT
{
   cacheItem * item;  //cacheItem is defined in cache.h
   cacheNodeT * prev;
   cacheNodeT * next;
};

//The keyword static here makes these global variables
//and functions inaccessible outside of this file.

//pointer to the first node in the list
static cacheNodeT * first;   

//pointer to the last node in the list
static cacheNodeT * last;

//sum of the sizes of all of the items in the list
static int currSize;

//functions that are only accessible to functions
//in this file
static void evict();
static void moveToFront(cacheNodeT * ptr);

//cacheInit
//Initialize first and last to NULL and currSize to 0.
void cacheInit()
{
   //TODO
   return;
}

//findCacheItem
//Search through the cacheNodeT to try to find
//the item with the provided url.
//If found, move the node containing the cacheItem
//to the front of the list (using moveToFront)
//and return the pointer to the cacheItem.
//If not found, return NULL.
cacheItem * findCacheItem(char * url)
{
   //TODO
   //You'll need a cacheNodeT * variable to iterate through
   //the linked list of cache objects.
   //
   //Use strcmp to see if the url stored with the cacheItem
   //object matches the url passed into the function. (See 
   //definition of cacheItem in cache.h.)
   //
   //If you find a match, call moveToFront to move
   //the cacheItem object to the front of the list and
   //return a pointer to it.
   //
   return NULL;
}

//moveToFront
//Move the node pointed to by cacheNodeT to the
//front of the cacheNodeT. This is used to
//implement the LRU policy.  The most recently
//used cache item is in the front of the list.
//The least recently used is at the end.
void moveToFront(cacheNodeT * ptr)
{
   //TODO
   //You have three cases that you need to handle
   //a) the cacheNodeT object is already in the front of the list
   //   i.e., ptr == first
   //b) the cacheNodeT object is the last one in the list. In this
   //   case you'll need to update last and the next field of the new
   //   last object (as well as first and the new first object)
   //
   //c) not a) or b) (Drawing pictures helps!)
   return;
}

//addCacheItem
//This function takes a new item to add to the cache.
//You will need to dynamically allocate a cacheItem object 
//and a cacheNodeT object and initialize the fields of
//those objects. first will need to be modified to point
//to the new cacheNodeT object.  last will need to be modified
//if it is currently NULL.
//If the adding the item to the cache would cause the
//size of the cache to exceed MAX_CACHE_SIZE, evict is
//called to evict one or more items from the cache to make
//room. 
void addCacheItem(char * url, char * content, int size)
{
   //TODO
   //First, dynamically allocate a new cacheItem object
   //and initialize its content, size, and url fields
   //(See definition of cacheItem type in cache.h)
   //
   //Second, dynamically allocate a new cacheNodeT object.
   //It's item field will be set to the pointer to the new
   //cacheItem object.
   //
   //Third, insert the new cacheNodeT object into the linked list.
   //You have two cases to handle:
   //a) first is NULL
   //b) first isn't NULL
   //
   //Fourth if the new currSize is bigger than MAX_CACHE_SIZE
   //call evict to evict enough objects so that currSize <= MAX_CACHE_SIZE
   return;
}

//evict
//This function is called in order to evict items from the cache.
//In order to the implement the LRU policy, nodes are deleted from
//the end of the list until currSize is <= MAX_CACHE_SIZE
void evict()
{
   //TODO
   //Loop starting with the last cacheNodeT object and iterating
   //toward the first object. Delete the object pointed to
   //by last and set last to the new last cacheNodeT object. Adjust 
   //currSize each time a cacheNodeT object is deleted.  Repeat
   //while currSize is greater than MAX_CACHE_SIZE:
   return;
}

//useful for debugging
//Prints the list (url and size) in forward and backward direction
//to make sure the prev and next pointers are both correct.
void printCacheList()
{
   cacheNodeT * iter;
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
