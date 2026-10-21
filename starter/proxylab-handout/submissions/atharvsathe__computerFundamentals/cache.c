
#include "cache.h"
#include "csapp.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
/*
 * Andrew: assathe
 * Cache function implementations for web proxy
 */

#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

queue *create_cache();
void free_cache(queue *cache);
node *extract_from_cache(queue *cache, char *key);
void insert_to_cache(queue *cache, char* key, char* object, size_t node_size);

void move_to_head(queue *cache, node *current);
void insert_at_head(queue *cache, node *current);
void evict_last(queue *cache);

/*
 * Creates empty linked list
 * Arguments: None
 * Returns: pointer to the linked list
 */
queue *create_cache() {
	queue *cache = Malloc(sizeof(queue));
	cache->size = 0;
	cache->head = NULL;
	cache->tail = NULL;

	return cache;
}

/*
 * Frees the memory occupied by cache
 * Arguments: cache pointer to be freed
 * Returns: None
 */
void free_cache(queue *cache) {
	if (cache != NULL) {
		node *current = cache->head;
		while(current != NULL) {
			node *temp = current;
			current = current->next;

			Free(temp->key);
			Free(temp->object);
		}
		Free(cache);
	}
}


/*
 * Checks if required uri is present in cache.
 * Arguments: cache pointer, uri to be found
 * Returns: Node pointer if found, NULL otherwise
 */
node *extract_from_cache(queue *cache, char *key) {
	if (cache != NULL) {
		node *current = cache->head;
		while(current != NULL) {
			if (strcmp(current->key, key) == 0) {
				move_to_head(cache, current);
				return current;
			}
			current = current->next;
		}
		return NULL;
	}
	return NULL;
}

/*
 * Inserts node to the head of the linked list
 * Arguments: cache pounter, uri, response, response size.
 * Returns: None
 */
void insert_to_cache(queue *cache, char* key, char* object, size_t node_size) {
	//makes space in cache
	while (cache->size + node_size > MAX_CACHE_SIZE) {
		evict_last(cache);
	}

	//creates node and assigns all values
	node *new = Malloc(sizeof(node));
	new->key = Malloc((strlen(key) + 1) * sizeof(char));;
	new->object = Malloc(node_size * sizeof(char));

	new->node_size = node_size;
	new->is_transmitting = false;
	strncpy(new->key, key, strlen(key) + 1);
	memcpy(new->object, object, node_size * sizeof(char));
	
	insert_at_head(cache, new);
	cache->size = cache->size + node_size;
}

/* Private functions */
/*
 * Moves the node to the head of the cache linked list
 * Arguments: cache pointer, node pointer to be moved
 * Returns: None
 */
void move_to_head(queue *cache, node *current) {
	//current is head
	if (current->prev == NULL)
		return;

	//current is tail
	if (current->next == NULL) {
		current->prev->next = NULL;
		cache->tail = current->prev;
	} else {
		current->next->prev = current->prev;
		current->prev->next = current->next;
	}
	
	insert_at_head(cache, current);
}

/*
 * Inserts the node to the head of the cache linked list;
 * Arguments: cache pointer, node pointer to be moved
 * Returns: None
 */
void insert_at_head(queue *cache, node *current) {
	//cache is empty
	if (cache->head == NULL) {
		cache->head = current;
		cache->tail = current;
		cache->head->next = NULL;
		cache->head->prev = NULL;
		return;
	}

	current->next = cache->head;
	current->prev = NULL;
	cache->head->prev = current;
	cache->head = current;
}

/*
 * Removes the last node in cache linked list
 * ArgumentsL cache pointer
 * Returns: None
 */
void evict_last(queue *cache) {
	if (cache != NULL) {
		node *last = cache->tail;
		if (last->is_transmitting == true)
			return;

		cache->tail = last->prev;
		cache->tail->next = NULL;
		cache->size = cache->size - last->node_size;

		Free(last->key);
		Free(last->object);
		Free(last);
	}
}











