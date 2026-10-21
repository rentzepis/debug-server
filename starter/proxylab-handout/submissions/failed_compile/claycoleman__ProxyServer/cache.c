#include "cache.h"

cache_object_t* delete_cache_object_from_head(cache_t* cache);
cache_object_t* delete_cache_object(cache_t* cache, char* object_id);
cache_object_t* init_cache_object(char* object_id, int length);
cache_object_t* search_for_cache_object(cache_t* cache, char* object_id);
void add_cache_object_to_tail_sync(cache_t* cache, cache_object_t* node);
void add_cache_object_to_tail(cache_t* cache, cache_object_t* node);
void destory_cache_object(cache_object_t* object);
void evict_cache_object(cache_t* cache);
void send_object_to_tail(cache_t* cache, char* object_id);

void wait_read(cache_t* cache) { P(&(cache->read)); }
void wait_write(cache_t* cache) { P(&(cache->write)); }
void signal_read(cache_t* cache) { V(&(cache->read)); }
void signal_write(cache_t* cache) { V(&(cache->write)); }

cache_t* init_cache() {
  cache_t* cache = (cache_t*) malloc(sizeof(cache_t));
  // Initiate cache.
  cache->head = NULL;
  cache->tail = NULL;
  cache->remaining_length = MAX_CACHE_SIZE;
  // Initiate synchonization primitives.
  Sem_init(&cache->read, 0, 1);
  Sem_init(&cache->write, 0, 1);
  cache->read_count = 0;
  return cache;
}

int read_data_from_cache(
  cache_t* cache,
  char* object_id,
  void* data,
  unsigned int* length
) {
  // Only allow access if cache has been initialized.
  if (cache != NULL) {
    // Lock synchonization primitives
    wait_read(cache);
    (cache->read_count)++;
    if (cache->read_count == 1) {
      wait_write(cache);
    }
    signal_read(cache);
    // Read object from cache
    cache_object_t* node = search_for_cache_object(cache, object_id);
    // Process result
    if (node == NULL) {
      // Not found, finish locks and send error
      wait_read(cache);
      (cache->read_count)--;
      if (cache->read_count == 0) {
        signal_write(cache);
      }
      signal_read(cache);
      // Send error
      return -1;
    } else {
      // Object found, copy to output.
      *length = node->length;
      memcpy(data, node->data, *length);
      // Release locks
      wait_read(cache);
      (cache->read_count)--;
      if (cache->read_count == 0) {
        signal_write(cache);
      }
      signal_read(cache);
      // Send object to tail.
      send_object_to_tail(cache, object_id);
      // Return success
      return 0;
    }
  } else {
    return -1;
  }
}

int add_data_to_cache(
  cache_t* cache,
  char* object_id,
  void* data,
  unsigned int length
) {
  // Only allow execution on non-empty cache.
  if (cache != NULL) {
    // Initialize new object
    cache_object_t* node = init_cache_object(object_id, length);
    // Copy over data
    memcpy(node->data, data, length);
    node->length = length;
    // Add object to tail
    add_cache_object_to_tail_sync(cache, node);
    return 0;
  } else {
    return -1;
  }
}

void send_object_to_tail(
  cache_t* cache,
  char* object_id
) {
  // Put object to end of cache. "Hot"
  wait_write(cache);
  add_cache_object_to_tail(cache, delete_cache_object(cache, object_id));
  signal_write(cache);
}

cache_object_t* init_cache_object(
  char* object_id,
  int length
) {
  // Initialize new cache object from malloc
  cache_object_t* node = (cache_object_t*) malloc(sizeof(cache_object_t));
  node->id = (char*) Malloc(sizeof(char) * (strlen(object_id) + 1));
  // Copy over data and return
  strcpy(node->id, object_id);
  node->length = 0;
  node->data = Malloc(length);
  node->next = NULL;
  // Return initialized node
  return node;
}

cache_object_t* search_for_cache_object(
  cache_t* cache,
  char* object_id
) {
  if (cache != NULL) {
    // start search at head and cycle through until tail
    cache_object_t* current_node = cache->head;
    while (current_node != NULL) {
      if (strcmp(current_node->id, object_id) == 0) {
        // Correct node found
        return current_node;
      }
      // Go to next node
      current_node = current_node->next;
    }
    return NULL;
  } else {
    return NULL;
  }
}

void add_cache_object_to_tail(
  cache_t* cache,
  cache_object_t* node
) {
  // Only perform execution on initialized cache
  if (cache != NULL) {
    if (cache->head == NULL) {
      // Cache is empty
      cache->head = node;
      cache->tail = node;
      cache->remaining_length -= (node->length);
    } else {
      // Cache is not empty
      cache->tail->next = node;
      cache->tail = node;
      cache->remaining_length -= (node->length);
    }
  }
}

void add_cache_object_to_tail_sync(
  cache_t* cache,
  cache_object_t* object
) {
  // Only perform execution on initialized cache
  if (cache != NULL) {
    // Obtain locks
    wait_write(cache);
    while (cache->remaining_length < object->length) {
      // Clear out as much data as we need
      evict_cache_object(cache);
    }
    // Add object to cache
    add_cache_object_to_tail(cache, object);
    // Release locks
    signal_write(cache);
  }
}

cache_object_t* delete_cache_object_from_head(cache_t* cache) {
  // Only perform execution on initialized cache
  if (cache != NULL) {
    // Get the node to delete
    cache_object_t* current_node = cache->head;
    if (current_node != NULL) {
      // Detach from linked list and adjust head
      cache->head = current_node->next;
      cache->remaining_length += (current_node->length);
      if (current_node == cache->tail) {
        // Fix empty base case
        cache->tail = NULL;
      }
      return current_node;
    } else {
      // Cache is empty
      return NULL;
    }
  } else {
    // Cache not initialized
    return NULL;
  }
}

void evict_cache_object(cache_t* cache) {
  // Only perform execution on initialized cache
  if (cache != NULL) {
    // Manage queue pointers and get node
    cache_object_t* node = delete_cache_object_from_head(cache);
    // Free data.
    Free(node->data);
    Free(node->id);
    Free(node);
  }
}

cache_object_t* delete_cache_object(
  cache_t* cache,
  char* object_id
) {
  // Only perform execution on initialized cache
  if (cache != NULL) {
    // Cycle through cache and find correct object_id.
    cache_object_t* current_node = cache->head;
    // Keep track of previous node to fix pointers when found
    cache_object_t* previous_node = NULL;
    // Cycle through cache
    while (current_node != NULL) {
      // Test object object_id
      if (strcmp(current_node->id, object_id) == 0) {
        // Correct pointers in structure, take care of edge cases
        if (cache->head == current_node) {
          cache->head = (current_node->next);
        }
        if (cache->tail == current_node) {
          cache->tail = previous_node;
        }
        // Use that previous_node we saved
        if (previous_node != NULL) {
          previous_node->next = (current_node->next);
        }
        // Don't let caller access our next pointer
        current_node->next = NULL;
        // Adjust cache globals
        cache->remaining_length += (current_node->length);
        // Return found node
        return current_node;
      } else {
        // Next iteration
        previous_node = current_node;
        current_node = current_node->next;
      }
    }
    return NULL;
  } else {
    // Cache is not initialized
    return NULL;
  }
}
