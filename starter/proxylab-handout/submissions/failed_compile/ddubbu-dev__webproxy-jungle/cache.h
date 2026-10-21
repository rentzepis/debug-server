#include "csapp.h"
#define METHOD_MAX_LEN 8
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// [확장성 대응] request 식별을 위한 속성이 늘어날 것을 대비
typedef struct RequestInfo {
    char method[METHOD_MAX_LEN];
    char path[MAXLINE];
} RequestInfo;

typedef struct ResponseInfo {
    // char *header;  // TODO: 현재는 직접 생성 중
    char *body;
    int body_size;
} ResponseInfo;

typedef struct CacheNode {
    RequestInfo req;
    ResponseInfo res;
    struct CacheNode *prev;
    struct CacheNode *next;
} CacheNode;

typedef struct CacheList {
    CacheNode *head;
    int sum_of_cache_object_size;
} DLL;

DLL *createDoublyLinkedList();
CacheNode *search(DLL *dll, RequestInfo req);
void pushFront(DLL *dll, RequestInfo req, ResponseInfo res);
void moveFront(DLL *dll, CacheNode *move_node);
