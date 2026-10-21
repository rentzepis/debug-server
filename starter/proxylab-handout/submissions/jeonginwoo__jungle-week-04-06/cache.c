#include "csapp.h"
#include "cache.h"

Dll *newDll()
{
    Dll *new_dll = (Dll *)calloc(1, sizeof(Dll));
    node *head_node = (node *)calloc(1, sizeof(node));
    new_dll->head = head_node;
    head_node->next = head_node->prev = head_node;
    new_dll->size = 0;
    return new_dll;
}

void pushFront(Dll *dll, node *push_node)
{    
    push_node->next = dll->head->next;
    dll->head->next->prev = push_node;
    dll->head->next = push_node;
    push_node->prev = dll->head;

    dll->size++;

    // 캐시가 가득일 때
    if (dll->size * sizeof(node) > MAX_CACHE_SIZE) {
        popBack(dll);
    }
    printf("push front (%s | %s) : ", push_node->cache_key.method, push_node->cache_key.path);
    printDll(dll);
}

cacheKey popBack(Dll *dll)
{
    if (dll->size == 0){
        printf("List is empty, cannot pop.\n");
        return (cacheKey){};
    }
    node *pop_node = dll->head->prev;
    cacheKey pop_key = pop_node->cache_key;

    dll->head->prev = pop_node->prev;
    pop_node->prev->next = dll->head;
    
    free(pop_node->cache_key.path);
    free(pop_node->data);
    free(pop_node);

    dll->size--;

    printf("pop back : ");
    printDll(dll);
    return pop_key;
}

node *search(Dll *dll, cacheKey cache_key)
{
    node *find_node = dll->head->next;
    printf("\n=== search ===\n");
    printDll(dll);
    printf("cache_key.method: %s\n", cache_key.method);
    printf("cache_key.path: %s\n", cache_key.path);
    printf("=== search ===\n\n");
    printf("\n=== find_node ===\n");
    while (find_node != dll->head) {
        printf("find_node.method: %s\n", find_node->cache_key.method);
        printf("find_node.path: %s\n", find_node->cache_key.path);
        if (!strcmp(find_node->cache_key.method, cache_key.method) && !strcmp(find_node->cache_key.path, cache_key.path)) {
            printf("search (%s | %s) : find!\n", cache_key.method, cache_key.path);
            return find_node;
        }
        find_node = find_node->next;
    }
    printf("=== find_node ===\n\n");
    printf("search (%s | %s) : not exist\n", cache_key.method, cache_key.path);
    return NULL;
}

void moveFront(Dll *dll, node *move_node)
{
    if (move_node == NULL) {
        printf("can't move\n");
        return;
    }
    move_node->prev->next = move_node->next;
    move_node->next->prev = move_node->prev;

    move_node->next = dll->head->next;
    dll->head->next->prev = move_node;
    dll->head->next = move_node;
    move_node->prev = dll->head;
    printDll(dll);
}

void printDll(Dll *dll)
{
    if (dll->size == 0)
    {
        printf("[]\n");
        return;
    }

    node *now = dll->head->next;
    while (now != dll->head->prev)
    {
        printf("(%s | %s) ←→ ", now->cache_key.method, now->cache_key.path);
        now = now->next;
    }
    printf("(%s | %s)\n", now->cache_key.method, now->cache_key.path);
}

void deleteList(Dll *dll)
{
    node *delete_node = dll->head->next;
    while (delete_node != dll->head) {
        node *next_node = delete_node->next;
        free(delete_node);
        delete_node = next_node;
    }
    free(dll->head);
    free(dll);
}

// int main()
// {
//     dll *dll = newDll();
//     pushFront(dll, (cacheKey){"GET", "/1111.html"});
//     pushFront(dll, (cacheKey){"GET", "/2222.html"});
//     pushFront(dll, (cacheKey){"GET", "/3333.html"});
//     pushFront(dll, (cacheKey){"GET", "/4444.html"});
//     pushFront(dll, (cacheKey){"GET", "/5555.html"});
//     popFront(dll);
//     popFront(dll);
//     popFront(dll);
//     search(dll, (cacheKey){"GET", "/6666.html"});
//     search(dll, (cacheKey){"GET", "/aaaa.html"});
//     // moveFront(dll, (cacheKey){"GET", "/1111.html"});
//     // moveFront(dll, (cacheKey){"GET", "/6666.html"});
//     deleteList(dll);
//     return 0;
// }