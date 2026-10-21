#define INITIAL_SIZE 10
#define MAX_CACHE_SIZE 1049000

typedef struct node{
    struct node *prev;
    struct node *next;
    char *key;
    char *val;
    int n;
}node_t;

typedef struct{
    size_t size;
    node_t *head;
    node_t *tail;
    sem_t mutex;
    sem_t w;
    int readcnt;
}cache_t;

void init_cache(cache_t *cachep);
int get_cache(cache_t *cachep, char *key, char *val);
void insert_cache(cache_t *cachep, char *key, char *value, int n);

