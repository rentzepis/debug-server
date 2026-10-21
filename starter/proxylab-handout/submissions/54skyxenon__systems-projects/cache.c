#include "cache.h"

static int active_readers;
static int cache_size;
static int cache_capacity;
static CacheEntry** cache;
static sem_t r_mutex; // For active readers count
static sem_t w_mutex; // For any write operation

/* Common helper to just obtain the writer lock */
static void acquire_writer_lock() {
    P(&w_mutex);
}

/* Inverse of above */
static void release_writer_lock() {
    V(&w_mutex);
}

/*
 * We still lock the reader count each time, BUT we don't need to acquire the writing lock multiple times.
 */
static void acquire_reader_lock() {
    P(&r_mutex);
    active_readers++;

    // first in
    if (active_readers == 1) {
        acquire_writer_lock();
    }

    V(&r_mutex);
}

/* Inverse of above */
static void release_reader_lock() {
    P(&r_mutex);
    active_readers--;

    // last out
    if (active_readers == 0) {
        release_writer_lock();
    }

    V(&r_mutex);
}

/*
 * Checks if the cache has enough space to add a data object of `size` bytes.
 *
 * ! Assumes we're calling in a context that has the writer lock acquired.
 */
static int has_room_for(size_t size) {
    size_t used_size = 0;
    for (int i = 0; i < cache_size; i++) {
        used_size += cache[i]->size;
    }

    size_t leftover_size = MAX_CACHE_SIZE - used_size;
    return leftover_size >= size;
}

/*
 * Does as the name says.
 *
 * ! Assumes we're calling in a context that has the writer lock acquired.
 */
static void evict_oldest_entry() {
    assert(cache_size >= 1);

    // find it first
    int oldest_entry_index = 0;
    int oldest_entry_timestamp = cache[0]->timestamp;
    for (int i = 1; i < cache_size; i++) {
        if (cache[i]->timestamp < oldest_entry_timestamp) {
            oldest_entry_timestamp = cache[i]->timestamp;
            oldest_entry_index = i;
        }
    }

    // then free/delete and shift everything up
    Free(cache[oldest_entry_index]->data);
    Free(cache[oldest_entry_index]);
    for (int i = oldest_entry_index; i < cache_size - 1; i++) {
        cache[i] = cache[i + 1];
    }
    cache_size--;
}

/* ===== BEGIN PUBLIC FUNCTIONS (see .h file for documentation) ===== */

void init_cache() {
    active_readers = 0;

    cache_size = 0;
    cache_capacity = CACHE_INITIAL_CAPACITY;
    cache = (CacheEntry**) Malloc(sizeof(CacheEntry*) * CACHE_INITIAL_CAPACITY);

    Sem_init(&r_mutex, 0, 1);
    Sem_init(&w_mutex, 0, 1);
}

void bump_timestamp_for_cached_uri(char* uri) {
    acquire_writer_lock();

    for (int i = 0; i < cache_size; i++) {
        if (!strcmp(cache[i]->uri, uri)) {
            cache[i]->timestamp = (int)time(NULL);
            break;
        }
    }

    release_writer_lock();
}

CacheEntry* get_cached_uri_entry(char* uri) {
    CacheEntry* entry = NULL;

    acquire_reader_lock();
    for (int i = 0; i < cache_size; i++) {
        if (!strcmp(cache[i]->uri, uri)) {
            entry = cache[i];
            break;
        }
    }
    release_reader_lock();

    return entry;
}

void add_cached_uri_entry(CacheEntry* entry) {
    acquire_writer_lock();

    // make room
    while (!has_room_for(entry->size)) {
        evict_oldest_entry();
    }

    // expand vector capacity if necessary
    if (cache_size == cache_capacity) {
        cache_capacity *= 2;
        cache = Realloc(cache, sizeof(CacheEntry*) * cache_capacity);
    }
    cache[cache_size++] = entry;

    release_writer_lock();
}

/* ===== END PUBLIC FUNCTIONS ===== */
