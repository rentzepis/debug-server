/* 
 cache.c for proxy lab
 ----------------------
 Designer: Guanting Liu
 Andrew ID: guantinl
 Date: April 28, 2014
 ----------------------
 Contains function definitions.
 
   About cache design
 ----------------------
    The whole cache is made up of individual cache items that completely 
 stores an HTTP/1.0 GET response. Each response is identified by their 
 corresponding URI in its original GET request. There's a bi-directional 
 link list that keeps track of all the cached items. Each New cache item 
 would be inserted to the head of the list. The most recenlty re-used 
 item would also be relocated to the head of the list. So the least 
 recently used item would automatically fall to the tail of the list 
 until it is evicted.

    Before inserting the new cache item into the head, the program first 
 build this cache item in the heap. Then it checks if the unused size of 
 the cache is smaller than the content size of this cache item. It would 
 keep evicting the least rencently used (LRU) cache item (the one at the 
 tail of the cache list) from the cache, until the ready-to-be-inserted 
 cache item would fit the unused size of the cache. Finally the new item 
 is inserted into cache.
	
	As per one of the requirements, multiple threads can read the cache 
 simultaneously, while only one thread is allowed to write the cache at 
 any time and no other threads can read the cache when it is writing. 
 In the contex of this program, reading means searching the cache for 
 the specific item. And writing means adding a new cached_item to the 
 cache, or using a piece of cached content and updating its recentness 
 after it has been found in the cache.

    Modifications to the cache are safely synchronized among threads by 
 reader-writer lock. Accesses to the cache are strictly thread-safe.
 */

#include "cache.h"



/* Inititialize an empty cache / cache_list (safe to call) */
void init_cache_list(Cache_List *cache_list){
	cache_list->cached_item_count = 0;
	cache_list->unused_size = MAX_CACHE_SIZE;
	cache_list->head = NULL;
	cache_list->tail = NULL;
}

/* Search the cache, if hit, copy content to the user buffer */
int search_and_get(Cache_List *cache_list, char *for_uri, void *usrbuf, 
		unsigned int *size) 
{
	Cache_Item *cache_item = NULL;

	Pthread_rwlock_rdlock(&cache_rwlock);	/* Lock for concurrent reading */

	cache_item = search_cache_item(cache_list, for_uri);	/* Reading */
	
	Pthread_rwlock_unlock(&cache_rwlock);	/* Unlocked reading */

	/* During this small transition period after releasing the reading lock 
	 * but before acquiring the writing lock, it is possible that the cache 
	 * item just found would be evicted by another writter who has been 
	 * waiting in the que. So it needs to be double checked before using. If 
	 * it is evicted, just treat it as a cache-miss.
	 */

	if (cache_item != NULL) {

		Pthread_rwlock_wrlock(&cache_rwlock);	/* Lock for exlusive writing */

		/* Check the item again in case it has been evicted instantaneously */
		if (strcmp(cache_item->uri, for_uri) == 0) {
			/* If it is still there, treat it as a cache-hit and use it */
			use_cache_item(cache_list, cache_item, usrbuf, size);
			print_cache_status(cache_list);

			Pthread_rwlock_unlock(&cache_rwlock);	/* Unlock writing */

			return 0;
		}
		else {
			/* If it is lost, treated it as a cache-miss */

			Pthread_rwlock_unlock(&cache_rwlock);	/* Unlock writing */

			return -1;
		}
	}
	else {
		/* Cache-miss */
		return -1;
	}
}

/* Search the cache for the target response item corresponidng to the URI.
 * Sub-function of search_and_get().
 */
Cache_Item *search_cache_item(Cache_List *cache_list, char *for_uri) {
	if (DEBUG_MODE) printf("  search_cache_item():\n");
	/* If Cache is empty */
	if (cache_list->head == NULL) {
		if (DEBUG_MODE) printf("    Empty cache.\n");
		if (DEBUG_MODE) printf("  search_cache_item() finish.\n");
		return NULL;
	}
	Cache_Item *cache_item = cache_list->head;
	while (cache_item->next_item != NULL) {
		/* Check cache items */
		if (strcmp(cache_item->uri, for_uri) == 0) {
			/* Cache hit! */
			if (DEBUG_MODE) printf("  search_cache_item() finish.\n");	
			return cache_item;
		}
		cache_item = cache_item->next_item;
	}
	/* Check last item */
	if (strcmp(cache_item->uri, for_uri) == 0) {
		/* Cache hit! */
		if (DEBUG_MODE) printf("  search_cache_item() finish.\n");
		return cache_item;
	}
	/* Cache miss. */
	if (DEBUG_MODE) printf("  search_cache_item() finish.\n");
	return NULL;
}

/* Build a new cache_item */
Cache_Item *build_cache_item(char *from_uri, char *from_content, 
		unsigned int length) 
{
	if (DEBUG_MODE) printf("    build_cache_item():\n");
	char *uri, *content;
	Cache_Item *cache_item;

	/* Malloc space for URI string, including one byte for null terminator */
	if ((uri = (char *) malloc(strlen(from_uri) + 1)) == NULL) {
		/* Abort caching if out of memory */
		if (DEBUG_MODE) printf("    build_cache_item() failed.\n");
		return NULL;
	}
	strcpy(uri, from_uri);

	/* Malloc space for content  */
	if ((content = (char *) malloc(length)) == NULL) {
		/* Abort caching if out of memory */
		Free(uri);
		if (DEBUG_MODE) printf("    build_cache_item() failed.\n");
		return NULL;
	}
	memcpy(content, from_content, length);

	/* Malloc space for cache_item  */
	if ((cache_item = malloc(sizeof(Cache_Item))) == NULL) {
		/* Abort caching if out of memory */
		Free(uri);
		Free(content);
		if (DEBUG_MODE) printf("    build_cache_item() failed.\n");
		return NULL;
	}
	cache_item->uri = uri;
	cache_item->content = content;
	cache_item->content_length = length;
	if (DEBUG_MODE) printf("    build_cache_item() finish.\n");

	return cache_item;
}

/* Add a new cache item into the cache */
void add_cache_item(Cache_List *cache_list, char *uri, char *content, 
		unsigned int size)
{
	if (DEBUG_MODE) printf("  add_cache_item():\n");
	Cache_Item *cache_item = build_cache_item(uri, content, size);

	/* Abort caching if build_cache_item failed */
	if (cache_item == NULL) {
		if (DEBUG_MODE) printf("  add_cache_item() failed.\n");
		return;
	}

	Pthread_rwlock_wrlock(&cache_rwlock);	/* Lock for exlusive writing */
												/* Writing block */
	while (cache_list->unused_size < size) {
		evict_cache_item(cache_list);
	}
	insert_item_to_listhead(cache_list, cache_item);
	printf("\tResponse content (%u bytes) for URI: %s has been cached.\n",
			size, uri);
	print_cache_status(cache_list);
												/* End of writing block */
	Pthread_rwlock_unlock(&cache_rwlock);	/* Unlock writing */

	if (DEBUG_MODE) printf("  add_cache_item() finish.\n");
}

/* Permenantly evict a cache item from the cache list and destroying 
   its content */
void evict_cache_item(Cache_List *cache_list) {
	if (DEBUG_MODE) printf("    evict_cache_item():\n");
	Cache_Item *cache_item = remove_item_from_list(cache_list, 
			cache_list->tail);
	free(cache_item->content);
	free(cache_item->uri);
	free(cache_item);
	if (DEBUG_MODE) printf("    evict_cache_item() finish.\n");
}

/* Remove a cache item from the cache list, but not destoying it */
Cache_Item *remove_item_from_list(Cache_List *cache_list, 
		Cache_Item *cache_item) 
{
	if (DEBUG_MODE) printf("      remove_item_from_list():\n");
	/* If it is the only item */
	if (cache_item->prev_item == NULL && cache_item->next_item == NULL) {
		cache_list->head = NULL;
		cache_list->tail = NULL;
	}
	/* If it is the head item */
	else if (cache_item->prev_item == NULL) {
		cache_list->head = cache_item->next_item;
		cache_item->next_item->prev_item = NULL;
	}
	/* If it is the tail item */
	else if (cache_item->next_item == NULL) {
		cache_list->tail = cache_item->prev_item;
		cache_item->prev_item->next_item = NULL;
	}
	/* If it is an item in the middle of the list */
	else {
		cache_item->next_item->prev_item = cache_item->prev_item;
		cache_item->prev_item->next_item = cache_item->next_item;
	}
	cache_list->cached_item_count--;
	cache_list->unused_size += cache_item->content_length;
	if (DEBUG_MODE) printf("      remove_item_from_list() finish.\n");
	return cache_item;
}

/* Insert an existing cache item into the cache list */
Cache_Item *insert_item_to_listhead(Cache_List *cache_list, 
		Cache_Item *cache_item) 
{
	if (DEBUG_MODE) printf("      insert_item_to_listhead():\n");
	/* If cache is empty */
	if (cache_list->head == NULL && cache_list->tail == NULL) {
		cache_item->next_item = NULL;
		cache_item->prev_item = NULL;
		cache_list->head = cache_item;
		cache_list->tail = cache_item;
	}
	/* If cache is not empty */
	else {
		cache_list->head->prev_item = cache_item;
		cache_item->next_item = cache_list->head;
		cache_item->prev_item = NULL;
		cache_list->head = cache_item;
	}
	cache_list->cached_item_count++;
	cache_list->unused_size -= cache_item->content_length;
	if (DEBUG_MODE) printf("      insert_item_to_listhead() finish.\n");
	return cache_item;
}

/* Use an already existed cached item */
void use_cache_item(Cache_List *cache_list, Cache_Item *cache_item, 
		void *usrbuf, unsigned int *size) 
{
	if (DEBUG_MODE) printf("    use_cache_item():\n");
	remove_item_from_list(cache_list, cache_item);
	insert_item_to_listhead(cache_list, cache_item);
	*size = cache_item->content_length;
	/* Copy the content data into user-buffer */
	memcpy(usrbuf, cache_item->content, *size);
	if (DEBUG_MODE) printf("    use_cache_item() finish.\n");
}

/* Print the utilization status of the cache */
void print_cache_status(Cache_List *cache_list) {
	printf("\n\t(Cached items: %u\tFree cache: %u bytes, %u%%)\n\n", 
		cache_list->cached_item_count, 
		cache_list->unused_size, 
		cache_list->unused_size * 100 / MAX_CACHE_SIZE);
	check_cache_consistency(cache_list);
}

/* Check the bi-directional consistency of the cache_list in a simple way */
/* (Useful for quickly identifying problems when debugging) */
void check_cache_consistency(Cache_List *cache_list) {
	unsigned int cnt1 = 0, cnt2 = 0;
	Cache_Item *cache_item;
	/* Count cache_items in forward direction */
	if (cache_list->head != NULL) {
		cache_item = cache_list->head;
		cnt1++;
		while (cache_item->next_item != NULL) {
			cache_item = cache_item->next_item;
			cnt1++;
		}
	}
	/* Count cache_items in backward direction */
	if (cache_list->tail != NULL) {
		cache_item = cache_list->tail;
		cnt2++;
		while (cache_item->prev_item != NULL) {
			cache_item = cache_item->prev_item;
			cnt2++;
		}
	}
	if (cnt1 != cnt2 || cnt1 != cache_list->cached_item_count) {
		printf("\tCache corrupted! Cached items: %u\tcnt1 :%u\tcnt2: %u\n", 
				cache_list->cached_item_count, cnt1, cnt2);
		exit(1);
	}
}



/******************************************
 * Wrappers for the pthread_rwlock_* functions 
 ******************************************/

int Pthread_rwlock_init(pthread_rwlock_t *rwlock,
		const pthread_rwlockattr_t *attr) 
{
	int i = pthread_rwlock_init(rwlock, attr);
	if (i != 0) unix_error("Pthread_mutex_lock error");
    return i;
}

int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
	int i = pthread_rwlock_rdlock(rwlock);
    if (i != 0) unix_error("Pthread_rwlock_rdlock error");
    return i;
}

int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
	int i = pthread_rwlock_wrlock(rwlock);
	if (i != 0) unix_error("Pthread_rwlock_wrlock error");
    return i;
}

int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
	int i = pthread_rwlock_unlock(rwlock);
    if (i != 0) unix_error("Pthread_rwlock_unlock error");
    return i;
}