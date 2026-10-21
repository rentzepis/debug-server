#include "cache.h"
#include "csapp.h"

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pthread_mutex_t mutex;

cache_node *create_new_node(char *key, char *object, size_t cache_node_size);
queue *create_cache();
void free_queue(queue *cache_queue);
char *read_from_cache(queue *cache_queue, char *uri, size_t *size);
void move_to_front(queue *cache_queue, cache_node *cur_node);
bool is_hit(char *key, queue *cache_queue);
void remove_last(queue *cache_queue);
void insert(queue *cache_queue, cache_node *cur_node);
void evict(queue *cache_queue, cache_node *cur_node);
void in_cache(queue *cache_queue, char *key, char *object,
              size_t cache_node_size);

cache_node *create_new_node(char *key, char *object, size_t cache_node_size) {
    cache_node *new_node = (cache_node *)malloc(sizeof(cache_node));
    if (!new_node) {
        perror("Memory allocation failed for new_node");
        exit(EXIT_FAILURE);
    }

    new_node->cache_node_size = cache_node_size;
    new_node->key = (char *)malloc(sizeof(char) * (strlen(key) + 1));
    if (!new_node->key) {
        perror("Memory allocation failed for new_node->key");
        exit(EXIT_FAILURE);
    }

    strcpy(new_node->key, key);
    new_node->object = (char *)malloc(cache_node_size);
    if (!new_node->object) {
        perror("Memory allocation failed for new_node->object");
        exit(EXIT_FAILURE);
    }

    memcpy(new_node->object, object, cache_node_size);
    new_node->next = NULL;
    new_node->prev = NULL;
    return new_node;
}

queue *create_cache() {
    queue *cache_queue = (queue *)malloc(sizeof(queue));
    if (!cache_queue) {
        perror("Memory allocation failed for cache_queue");
        exit(EXIT_FAILURE);
    }

    cache_queue->queue_size = 0;
    cache_node *head = (cache_node *)malloc(sizeof(cache_node));
    if (!head) {
        perror("Memory allocation failed for cache_node");
        exit(EXIT_FAILURE);
    }
    head->next = NULL;
    head->prev = NULL;
    head->key = NULL;
    head->object = NULL;
    head->cache_node_size = 0;
    cache_queue->head = head;

    cache_node *tail = (cache_node *)malloc(sizeof(cache_node));
    if (!tail) {
        perror("Memory allocation failed for cache_node");
        exit(EXIT_FAILURE);
    }
    tail->cache_node_size = 0;
    tail->key = NULL;
    tail->next = NULL;
    tail->object = NULL;
    cache_queue->tail = tail;

    cache_queue->tail->prev = cache_queue->head;
    cache_queue->head->next = cache_queue->tail;

    pthread_mutex_init(&mutex, NULL);
    return cache_queue;
}

void free_queue(queue *cache_queue) {
    cache_node *cur_node = cache_queue->tail->prev;
    while (cur_node != cache_queue->head) {
        cache_node *prev_node = cur_node->prev;
        free(cur_node->key);
        free(cur_node->object);
        free(cur_node);
        cur_node = prev_node;
    }
    free(cache_queue->head);
    free(cache_queue->tail);
    free(cache_queue);
    return;
}

char *read_from_cache(queue *cache_queue, char *uri, size_t *size) {
    pthread_mutex_lock(&mutex);
    cache_node *cur_node;
    cache_node *result = NULL;
    for (cur_node = cache_queue->head->next; cur_node != cache_queue->tail;
         cur_node = cur_node->next) {
        if (strcmp(cur_node->key, uri) == 0) {
            result = cur_node;
            move_to_front(cache_queue, cur_node);
            break;
        }
    }

    if (result == NULL) {
        pthread_mutex_unlock(&mutex);
        return NULL;
    }

    char *response = (char *)malloc(sizeof(char) * (result->cache_node_size));
    if (!response) {
        perror("Memory allocation failed for response");
        exit(EXIT_FAILURE);
    }
    *size = result->cache_node_size;
    memcpy(response, result->object, result->cache_node_size);
    pthread_mutex_unlock(&mutex);
    return response;
}

void move_to_front(queue *cache_queue, cache_node *cur_node) {
    cur_node->next->prev = cur_node->prev;
    cur_node->prev->next = cur_node->next;

    cache_node *head = cache_queue->head;
    head->next->prev = cur_node;
    cur_node->next = head->next;
    head->next = cur_node;
    cur_node->prev = head;
}

bool is_hit(char *key, queue *cache_queue) {
    cache_node *cur_node = cache_queue->head->next;
    while (cur_node != cache_queue->tail) {
        if (strcmp(cur_node->key, key) == 0) {
            return true;
        }
        cur_node = cur_node->next;
    }
    return false;
}

void remove_last(queue *cache_queue) {
    cache_node *last = cache_queue->tail->prev;
    cache_queue->queue_size -= last->cache_node_size;
    if (cache_queue->queue_size < 0)
        printf("error queue size");
    last->prev->next = last->next;
    last->next->prev = last->prev;
    free(last->key);
    free(last->object);
    free(last);
}

void evicte(queue *cache_queue, cache_node *cur_node) {
    cache_node *last_node;
    last_node = cache_queue->tail->prev;

    while (last_node != cache_queue->head) {
        remove_last(cache_queue);
        size_t cur_size = cur_node->cache_node_size + cache_queue->queue_size;
        if (cur_size < MAX_CACHE_SIZE) {
            insert(cache_queue, cur_node);
            break;
        }
        last_node = cache_queue->tail->prev;
    }

    return;
}

void insert(queue *cache_queue, cache_node *cur_node) {
    // increase size
    cache_queue->queue_size += cur_node->cache_node_size;

    // insert in the front
    cache_node *next = cache_queue->head->next;
    next->prev = cur_node;
    cur_node->next = next;
    cur_node->prev = cache_queue->head;
    cache_queue->head->next = cur_node;
}

void in_cache(queue *cache_queue, char *key, char *object,
              size_t cache_node_size) {

    if (is_hit(key, cache_queue))
        return;

    pthread_mutex_lock(&mutex);
    size_t cur_size = cache_queue->queue_size + cache_node_size;
    cache_node *cur_node = create_new_node(key, object, cache_node_size);

    if (cur_size > MAX_CACHE_SIZE) {
        evicte(cache_queue, cur_node);
    } else {
        insert(cache_queue, cur_node);
    }

    pthread_mutex_unlock(&mutex);
}