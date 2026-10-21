
#include "cache.h"
#include "csapp.h"

static void lru_item_destroy(lru_item_t* item);
static void remove_item(lru_item_t* item);
static void move_item_front(lru_item_t* head, lru_item_t* item);

void lru_init(lru_t* lrup, int max_size)
{
    lrup->head = Malloc(sizeof(lru_item_t));
    lrup->tail = Malloc(sizeof(lru_item_t));
    lrup->head->next = lrup->tail;
    lrup->tail->prev = lrup->head;

    lrup->size = 0;
    lrup->max_size = max_size;
    Sem_init(&lrup->mutex, 0, 1);
}

void lru_destroy(lru_t* lrup)
{
    lru_item_t *item, *next;

    item = lrup->head->next;
    while (item != lrup->tail)
    {
        next = item->next;
        lru_item_destroy(item);
        item = next;
    }
    Free(lrup->head);
    Free(lrup->tail);
}

char* lru_get(lru_t* lrup, char* key, int* size)
{
    lru_item_t* item;
    char* resp = NULL;

    P(&lrup->mutex);
    item = lrup->head->next;
    while (item != lrup->tail)
    {
        if (!strcmp(key, item->key))
        {
            resp = item->resp;
            *size = item->size;
            remove_item(item);
            move_item_front(lrup->head, item);
            break;
        }
        item = item->next;
    }
    V(&lrup->mutex);

    return resp;
}

/* Return error code
-1: OOM
*/
int lru_put(lru_t* lrup, char* key, char* resp, int size)
{
    lru_item_t* item, *item_to_remove;

    item = Malloc(sizeof(lru_item_t));
    if (item == NULL)
        return -1;
    item->key = key;
    item->resp = resp;
    item->size = size;

    P(&lrup->mutex);
    while (lrup->size + size > lrup->max_size)
    {
        item_to_remove = lrup->tail->prev;
        lrup->size -= item_to_remove->size;
        remove_item(item_to_remove);
        lru_item_destroy(item_to_remove);
    }
    move_item_front(lrup->head, item);
    lrup->size += size;
    V(&lrup->mutex);

    return 0;
}

static void lru_item_destroy(lru_item_t* item)
{
    Free(item->key);
    Free(item->resp);
    Free(item);
}

static void remove_item(lru_item_t* item)
{
    lru_item_t* prev = item->prev;
    lru_item_t* next = item->next;

    prev->next = next;
    next->prev = prev;
}

static void move_item_front(lru_item_t* head, lru_item_t* item)
{
    lru_item_t* first = head->next;

    head->next = item;
    first->prev = item;
    item->prev = head;
    item->next = first;
}