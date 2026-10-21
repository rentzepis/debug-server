#include <semaphore.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

#define NCACHES 10
#define PACK_KEY(line, method, url) sprintf(line, "%s%s", method, url)
volatile static int gstamp = 1;

typedef struct {
    char content[MAX_OBJECT_SIZE];
    char key[MAXLINE];
    int stamp;
} object_t;

struct cache_t{
    sem_t mutex;
    object_t objects[NCACHES];
} cache;

void cache_init() {
    sem_init(&cache.mutex, 0, 1);
    for (size_t i = 0; i<NCACHES; ++i) 
        cache.objects[i].stamp = -1;
}

int cache_get(char *key, char *buf) {
    sem_wait(&cache.mutex);
    gstamp++;
    for (size_t i = 0; i<NCACHES; ++i) {
        if (strcasecmp(cache.objects[i].key, key) == 0) {
            strcpy(buf, cache.objects[i].content);
            cache.objects[i].stamp = gstamp;
            return 1;
        }
    }
    sem_post(&cache.mutex);
    return 0;
}

int cache_add(char *key, char *buf) {
    size_t idx = 0;
    sem_wait(&cache.mutex);
    gstamp++;
    for (size_t i = 0; i<NCACHES; ++i) {
        if (cache.objects[i].stamp == -1) {
            idx = i;
            break;
        }
        idx = cache.objects[idx].stamp < cache.objects[i].stamp ? idx: i;
    }
    strcpy(cache.objects[idx].key, key);
    strcpy(cache.objects[idx].content, buf);
    cache.objects[idx].stamp = gstamp;

    sem_post(&cache.mutex);
    return 0;
}
