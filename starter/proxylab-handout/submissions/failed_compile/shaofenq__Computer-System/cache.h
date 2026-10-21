/*
author: Shaofeng Qin, shaofenq
Main goal: this cache aims to store the url and server's reply in cache blocks such that
when client request some already existed url(key) in the cache, instead of retrieving from server(takes longer time)
cache will direcly send the saved reply from server to the client, which has higher efficiency
*/


//define some dimension limits constants or storage limits constants
/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)


//define the basic cache block structure
typedef struct cache_block
{
    /* each cache block store url as key, complete response made by a server to a client,
    including any response headers(char*), 
    */
   
};


