
//These values are very small, but they allow us to
//easily test the caching and the LRU policy
#define MAX_CACHE_SIZE 60000
#define MAX_OBJECT_SIZE 25000

//This is the only type that the proxy needs to be aware of
typedef struct
{
   char url[MAXLINE];
   int size;
   char * content;
} cacheItem;

//These functions will be called by code in proxy.c
void cacheInit();  
cacheItem * findCacheItem(char * url);
void addCacheItem(char * url, char * content, int size);
void printCacheList();
