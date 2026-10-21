#include "csapp.h"
#include "cache.h"

/* 캐시 초기화 */
cache *init_cache()
{
    cache *cache_list = (cache *)malloc(sizeof(struct cache_header));

    cache_list->front = NULL;
    cache_list->end = NULL;
    cache_list->empty_size = MAX_CACHE_SIZE;
    // int sem_init(sem_t *sem, int pshared, unsigned int value);
    // pshared를 0으로 설정하여 세마포어가 프로세스 내의 스레드간에 공유되도록 함. 다른 프로세스와는 공유되지 않음.
    // 세마포어의 초기값(value)을 1로 설정. 한 번에 하나의 스레드만이 캐시에 접근할 수 있도록 함
    Sem_init(&cache_list->mutex_w, 0, 1);
    Sem_init(&cache_list->mutex_r, 0, 1);
    cache_list->readcnt = 0;
    return cache_list;
}

/* 인자로 주어진 정보를 담은 노드 생성 */
node *new_node(char *url, char *data, int length)
{
    node *p;
    if (url == NULL || data == NULL)
        return NULL;
    if ((p = (node *)malloc(sizeof(struct cache_node))) == NULL)
        return NULL;

    // size
    p->size = length;

    // url
    if ((p->url = (char *)malloc(sizeof(char) * (strlen(url) + 1))) == NULL)
        return NULL;
    strcpy(p->url, url);

    // data
    if ((p->data = (char *)malloc(length)) == NULL)
        return NULL;
    memcpy(p->data, data, length);

    // next, prev
    p->next = NULL;
    p->prev = NULL;

    return p;
}

/* 캐시에서 노드 삭제하기 */
void delete_node(cache *list, node *p)
{
    if (p == NULL || list == NULL)
        return;

    if (p == list->front) // 리스트의 가장 앞에 있는 노드라면
    {
        if (p == list->end) // 리스트의 유일한 노드라면
        {
            list->front = NULL;
            list->end = NULL;
            return;
        }
        list->front = p->next;
        list->front->prev = NULL;
    }
    else if (p == list->end) // 리스트의 가장 마지막에 있는 노드라면
    {
        list->end = p->prev;
        list->end->next = NULL;
    }
    else // 리스트의 중간에 있는 노드라면
    {
        p->prev->next = p->next;
        p->next->prev = p->prev;
    }
    list->empty_size += p->size; // 삭제한 노드의 크기만큼 가용 사이즈 늘리기

    return;
}

/* 삭제한 노드의 메모리 해제하기 */
void free_node(node *p)
{
    if (p == NULL)
        return;

    if (p->url != NULL)
        Free(p->url);
    if (p->data != NULL)
        Free(p->data);
    Free(p);

    return;
}

/* url에 해당되는 노드 찾기 */
node *search_node(cache *list, char *url)
{
    node *p;
    if (url == NULL || list == NULL)
        return NULL;

    p = list->front;
    while (p != NULL)
    {
        if (strcmp(p->url, url) == 0)
            return p;
        p = p->next;
    }

    return NULL;
}

/* 연결 리스트의 맨 앞에 노드 삽입하기 */
void insert_node_front(cache *list, node *p)
{
    if (p == NULL || list == NULL)
        return;

    if (list->front == NULL) // 빈 리스트인 경우
    {
        list->front = p;
        list->end = p;
        p->next = NULL;
        p->prev = NULL;
        list->empty_size -= p->size;
        return;
    }

    p->next = list->front;
    list->front->prev = p;
    list->front = p;
    p->prev = NULL;
    list->empty_size -= p->size;

    return;
}

/* 연결 리스트의 맨 뒤에 노드 추가하기 */
void insert_node_end(cache *list, node *p)
{
    if (p == NULL || list == NULL)
        return;

    if (list->front == NULL) // 빈 리스트인 경우
    {
        list->front = p;
        list->end = p;
        p->next = NULL;
        p->prev = NULL;
        list->empty_size -= p->size;
        return;
    }

    p->prev = list->end;
    list->end->next = p;
    list->end = p;
    p->next = NULL;
    list->empty_size -= p->size;

    return;
}

/* url에 해당되는 노드 찾기 (LRU, 세마포어 적용)*/
node *find_cache(cache *list, char *url)
{
    if (list == NULL || url == NULL)
        return NULL;

    P(&(list->mutex_r));
    list->readcnt++;
    if (1 == list->readcnt)
        P(&(list->mutex_w));
    V(&(list->mutex_r));

    node *p = search_node(list, url);

    P(&(list->mutex_r));
    list->readcnt--;
    if (0 == list->readcnt)
        V(&(list->mutex_w));
    V(&(list->mutex_r));

    if (p == NULL)
        return NULL;

    P(&list->mutex_w);
    delete_node(list, p);
    insert_node_end(list, p);
    V(&list->mutex_w);

    return p;
}

/* 캐시 추가하기 */
void add_cache(cache *list, char *url, char *data, int length)
{
    node *p;

    // MAX_OBJECT_SIZE를 초과하면 추가하지 않고 리턴
    if (length > MAX_OBJECT_SIZE)
        return;

    p = new_node(url, data, length);
    if (p == NULL)
        return;

    P(&list->mutex_w);
    if (list->empty_size < p->size) // MAX_CACHE_SIZE를 초과하는 경우 가용 공간이 확보될 떄까지 evict
        evict_cache(list, p);
    insert_node_end(list, p);
    V(&list->mutex_w);

    return;
}

/* 캐시의 Maximum Size를 초과하는 경우 least-recently-used 노드 삭제 */
void evict_cache(cache *list, node *p)
{
    if (list == NULL || p == NULL)
        return;

    node *bp = list->front;
    P(&list->mutex_w);
    while (list->empty_size < p->size)
    {
        delete_node(list, bp);
        free_node(bp);
        bp = list->front;
    }
    V(&list->mutex_w);

    return;
}