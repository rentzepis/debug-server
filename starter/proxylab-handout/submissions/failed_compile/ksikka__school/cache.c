#include <stdlib.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "csapp.h"
#include "cache.h"



/*
 * cache.c
 * ksikka, mchoquet
 * A utility for a web proxy cache.
 * Keeps the responses in a linked list, keyed by strings.
 * Implements a least-recently-used eviction policy
 */

//typedef struct cache_key* keyptr;
typedef struct node* nodeptr;

struct node {
  nodeptr next;
  keyptr key;
  void* data;
  int size;
  unsigned long usedAt;
};



/* cahce state */
static nodeptr start; /* always points to a dummy header */
static int cacheMax;
static int dataMax;
static volatile int cacheSize;
static int initialized;
/* I keep a relative timer, and assume that it won't wrap around */
static unsigned long currTime;

/* locks */
static pthread_mutex_t timeMutex;
static pthread_rwlock_t rwLock;

/* function stubs */
static void markSeen(nodeptr current);
static nodeptr getNode(keyptr key);
static void freeNode(nodeptr ptr);
static int makeSpace(int spaceNeeded);
static keyptr copyKey(keyptr k);


/*
 * Initializes the cache with the given sizes.
 * Returns 0 on success, or else -1.
 * Not thread-safe.
 */
int initCache(int maxItemSize, int maxCacheSize) {
  if (initialized) return -1;
  if (maxItemSize > maxCacheSize) return -1;
  /* set up the cache header */
  start = malloc(sizeof(struct node));
  if (start == NULL) return -1;
  start->size = -1;
  start->key = NULL;
  start->data = NULL;
  start->next = NULL;
  /* set up the cache state */
  cacheMax = maxCacheSize;
  dataMax = maxItemSize;
  cacheSize = 0;
  initialized = 1;
  currTime = 0;
  /* set up the cache mutexes */
  pthread_mutex_init(&timeMutex, NULL);
  pthread_rwlock_init(&rwLock, NULL);
  return 0;
}


/*
 * Looks for the given key in the cache. If it doesn't find it, or the
 * key is expired, it returns NULL, else it returns the value.
 */
void* lookup(keyptr key, int* len) {
  if (!initialized || key == NULL || len == NULL) return NULL;
  pthread_rwlock_rdlock(&rwLock);
  nodeptr current = getNode(key);
  markSeen(current);
  pthread_rwlock_unlock(&rwLock);
  if (current == NULL) return NULL;
  *len = current->size;
  return current->data;
}

/* 
 * Looks for the given key in the list, and returns its node (or NULL).
 */
static nodeptr getNode(keyptr key) {
  nodeptr current = start->next;
  while (current != NULL) {
    keyptr otherKey = current->key;
    if (key->serverPort == otherKey->serverPort &&
        !strcmp(key->hostName, otherKey->hostName) &&
        !strcmp(key->filePath, otherKey->filePath)) {
      return current;
    }
    current = current->next;
  }
  return NULL;
}

/*
 * Mark the node as being seen at the current relative time, and
 * updates the relative time so the next event has a unique timestamp.
 */
static void markSeen(nodeptr current) {
  if (current == NULL) return;
  pthread_mutex_lock(&timeMutex);
  current->usedAt = currTime++;
  pthread_mutex_unlock(&timeMutex);
}

/*
 * 1. Looks through the cache to see if the key is already in it
 * 2. Updates it if needed, or else:
 *    a. makes space.
 *    b. inserts the node at the front of the list.
 * 4. Returns 0 on success, else -1
 * Makes a defensive copy of the key and value.
 */
int update(keyptr key, void* value, int valSize) {
  if (!initialized || key == NULL || value == NULL || valSize < 0) return -1;
  if (valSize > dataMax) return 0;
  /* acquire the reader lock */
  pthread_rwlock_wrlock(&rwLock);

  /* make a defensive copy of the value */
  void* vCopy = malloc(valSize);
  if (vCopy == NULL) return -1;
  memcpy(vCopy, value, valSize);
  nodeptr n = getNode(key);  

  if (n != NULL) {
    /* overwrite the data already in the cache */
    cacheSize = cacheSize - (n->size) + valSize;
    free(n->data);
    n->data = vCopy;
    n->size = valSize;
    markSeen(n);
  } else {
    /* make a new node that contains the data */
    if (makeSpace(valSize) == -1) return -1;
    n = malloc(sizeof(struct node));
    if (n == NULL) return -1;
    keyptr kCopy = copyKey(key);
    if (kCopy == NULL) return -1;
    n->key = kCopy;
    n->data = vCopy;
    n->size = valSize;
    markSeen(n);
    n->next = start->next;
    start->next = n;
    cacheSize += valSize;
  }
  pthread_rwlock_unlock(&rwLock);
  return 0;
}

/*
 * Frees the memory associated with the given node.
 */
static void freeNode(nodeptr ptr) {
  free(ptr->key->hostName);
  free(ptr->key->filePath);
  free(ptr->key);
  free(ptr->data);
  free(ptr);
}

/*
 * Makes a defensive copy of a key.
 */
static keyptr copyKey(keyptr k) {
  keyptr keyCopy = malloc(sizeof(struct cache_key));
  if (k == NULL || k->hostName == NULL || k->filePath == NULL) return NULL;
  char* newHost = malloc(strlen(k->hostName) + 1);
  strcpy(newHost, k->hostName);
  char* newPath = malloc(strlen(k->filePath) + 1);
  strcpy(newPath, k->filePath);
  keyCopy->hostName = newHost;
  keyCopy->filePath = newPath;
  keyCopy->serverPort = k->serverPort;
  return keyCopy;
}

/*
 * Repeatedly frees the oldest node until there is enough space.
 * Returns 0 if it succeeded, or -1 otherwise
 */
static int makeSpace(int spaceNeeded) {
  while (cacheSize + spaceNeeded > cacheMax) {
    /* find the oldest */
    nodeptr prev = start;
    nodeptr oldPrev = NULL;
    while (prev->next != NULL) {
      if (oldPrev == NULL || prev->next->usedAt < oldPrev->next->usedAt) {
        oldPrev = prev;
      }
      prev = prev->next;
    }
    if (oldPrev == NULL) return -1;
    /* remove the oldest */
    nodeptr oldest = oldPrev->next;
    cacheSize -= oldest->size;
    oldPrev->next = oldest->next;
    freeNode(oldest);
  }
  return 0;
}

