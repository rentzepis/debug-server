/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct CacheKey
{
    char method[8];
    char *path;
} cacheKey;

typedef struct Node
{
    cacheKey cache_key;
    char *data;
    int data_len;
    struct Node *prev;
    struct Node *next;
} node;

typedef struct DoublyLinkedList
{
    node *head;
    int size;
} Dll;

Dll *newDll();
void pushFront(Dll *dll, node *push_node);
cacheKey popBack(Dll *dll);
node *search(Dll *dll, cacheKey cache_key);
void moveFront(Dll *dll, node *move_node);
void printDll(Dll *dll);
void deleteList(Dll *dll);