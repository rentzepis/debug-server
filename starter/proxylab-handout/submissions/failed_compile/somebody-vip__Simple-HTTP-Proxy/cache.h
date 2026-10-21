/* 
 cache.h for proxy lab
 ----------------------
 Designer: Guanting Liu
 Andrew ID: guantinl
 Date: April 28, 2014
 ----------------------
 Contains cache related global variables, typedefs and function prototypes.

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

#include "csapp.h"

#define DEBUG_MODE 0	/* 0=off; 1=on, prints verbose message for debugging */

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* Cache related global variable(s) */
pthread_rwlock_t cache_rwlock;


   /*---------------------------------------*
	|	Sync functions reference:			|
	|										|
	|	pthread_mutex_t mutex;				|
	|	pthread_mutex_init(&mutex, 0);		|
	|	pthread_mutex_lock(&mutex);			|
	|	pthread_mutex_unlock(&mutex);		|
	|										|
	|	pthread_rwlock_t lock;				|
	|	pthread_rwlock_init(&lock, NULL);	|
	|	pthread_rwlock_rdlock(&lock);		|
	|	pthread_rwlock_wrlock(&lock);		|
	|	pthread_rwlock_unlock(&lock);		|
	*---------------------------------------*/

/* Cache_Item that tracks a piece of cached content */
typedef struct Cache_Item {
 	char *uri;			/* Treated as string with a null terminator */
 	char *content;		/* Treated as consecutive memory bytes */
 	unsigned int content_length;
 	struct Cache_Item *next_item;	/* Points to next Cache_Item */
 	struct Cache_Item *prev_item;	/* Points to previous Cache_Item */
} Cache_Item;

/* Cache_List stucture that tracks of all the Cache_Items */
typedef struct Cache_List {
	unsigned int cached_item_count;
	unsigned int unused_size;
	Cache_Item *head;	/* Points to first Cache_Item or null if empty cache */
	Cache_Item *tail;	/* Points to last Cache_Item or null if empty cache */
} Cache_List;



/*
 * Function prototypes
 */
void init_cache_list(Cache_List *cache_list);

int search_and_get(Cache_List *cache_list, char *for_uri, void *usrbuf, 
		unsigned int *size);

Cache_Item *search_cache_item(Cache_List *cache_list, char *for_uri);

Cache_Item *build_cache_item(char *from_uri, char *from_content, 
		unsigned int length);

void add_cache_item(Cache_List *cache_list, char *uri, char *content, 
		unsigned int size);

void evict_cache_item(Cache_List *cache_list);

Cache_Item *remove_item_from_list(Cache_List *cache_list, 
		Cache_Item *cache_item);

Cache_Item *insert_item_to_listhead(Cache_List *cache_list, 
		Cache_Item *cache_item);

void use_cache_item(Cache_List *cache_list, Cache_Item *cache_item, 
		void *usrbuf, unsigned int *size);

void print_cache_status(Cache_List *cache_list);

void check_cache_consistency(Cache_List *cache_list);

/* Wrappers for the pthread_rwlock_* functions */
int Pthread_rwlock_init(pthread_rwlock_t *rwlock,
		const pthread_rwlockattr_t *attr);

int Pthread_rwlock_rdlock(pthread_rwlock_t *rwlock);

int Pthread_rwlock_wrlock(pthread_rwlock_t *rwlock);

int Pthread_rwlock_unlock(pthread_rwlock_t *rwlock);