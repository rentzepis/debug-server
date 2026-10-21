#include "cache.h"
#include "csapp.h"

void init_cacheline(CacheLine* cacheline)
{
    cacheline->size = 0;
    cacheline->read_cnt = 0;
    sem_init(&(cacheline->mutex), 0, 1);
    sem_init(&(cacheline->access), 0, 1);
}

void init_cache(Cache* cache)
{
    cache->total_size = 0;
    for (int i=0; i<NCACHESLOTS; i++) {
        init_cacheline(cache->cachebuf + i);
    }
}

void init_cachebuf(CacheBuff* cb)
{
    cb->offset = 0;
    cb->buf = Malloc(MAX_OBJECT_SIZE);
}

void deinit_cachebuf(CacheBuff* cb)
{
    if (cb->buf) {
        Free(cb->buf);
        cb->buf = NULL;
    }        
}

time_t get_lastaccess(CacheLine* cl)
{
    time_t last_access;
    P(&cl->access);
    last_access = cl->last_access;
    V(&cl->access);
    return last_access;
}

const char* get_path(CacheLine* cl)
{
    char* path;
    P(&cl->access);
    path = cl->path;
    V(&cl->access);
    return path;
}

int read_cacheline(CacheLine* cl, char* buf)
{
    P(&cl->mutex);
    if (cl->read_cnt == 0)  // first reader
        P(&cl->access);
    cl->read_cnt += 1;
    V(&cl->mutex);

    cl->last_access = time(0);
    int size = cl->size;
    strncpy(buf, cl->buf, size);

    P(&cl->mutex);
    if (cl->read_cnt == 1)  // last reader
        V(&cl->access);
    cl->read_cnt -= 1;
    V(&cl->mutex);

    return size;
}

void write_cacheline(CacheLine*cl, char* buf, char* path, int size)
{
    P(&cl->access);
    cl->last_access = time(0);

    if (cl->size > 0) {     // non-empty line
        Free(cl->buf);
        Free(cl->path);
    }

    cl->size = size;
    cl->buf = (char*)Malloc(cl->size);
    cl->path = (char*)Malloc(strlen(path));
    strncpy(cl->buf, buf, size);
    strcpy(cl->path, path);

    V(&cl->access);
}

int read_cache(Cache* cache, char* path, char* buf)
{
    print_cache(cache);
    for(int i=0; i<NCACHESLOTS; i++) {
        const char* cache_path = get_path(&cache->cachebuf[i]);
        if (cache_path && strcmp(cache_path, path) == 0) {
            return read_cacheline(&cache->cachebuf[i], buf);
        }
    }  
    return 0;
}

// weak consistency
int write_cache(Cache* cache, char* path, char* buf, int size)
{
    if ((size + cache->total_size) > MAX_CACHE_SIZE) {
        fprintf(stderr, "Cache size exceed total limit");
        return 0;
    }

    // LRU evict
    int lru_i = -1;
    time_t least_time = time(0);
    for (int i=0; i<NCACHESLOTS; i++) {
        if (cache->cachebuf[i].last_access < least_time) {
            lru_i = i;
            least_time = cache->cachebuf[i].last_access;
        }
    }
    if (lru_i < 0) {
        fprintf(stderr, "Can not find proper slot\n");
        return 0;
    }
    write_cacheline(&cache->cachebuf[lru_i], buf, path, size);

    return 1;
}

void append_buf(CacheBuff* dest, char* src, int size)
{
    while (size-- > 0) {
        dest->buf[(dest->offset)++] = *(src++);
    }
}

/**
 * write to both fd and dest array, set exceed flag in exceed
 **/
void Rio_writeBuf(int fd, CacheBuff* dest, char* src, int size, int* exceed)
{
    Rio_writen(fd, src, size);
    if (*exceed) {
        // already exceed, do nothing
    } else if (dest->offset + size > MAX_OBJECT_SIZE) {        
        *exceed = 1;
    } else {
        append_buf(dest, src, size);
    }
}

void print_cache(Cache* cache) {
    printf("Cache Debug: \n");
    for (int i=0; i<NCACHESLOTS; i++) {
        if (cache->cachebuf[i].path) 
            printf(" cached path %s\n", cache->cachebuf[i].path);
    }
}