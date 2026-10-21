#include "csapp.h"

// 캐시와 객체 사이즈 정의
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// 캐싱 웹 객체
typedef struct web_object {
    char filename[MAXLINE];
    int content_len;
    char *response_ptr;
    struct web_object *prev, *next;
} web_object;

// cache 사용 함수
web_object *find_cache(char *filename);
void send_cache(web_object *web_object, int fd);
void update_cache(web_object *web_object);
void regi_cache(web_object *web_object);

// 전역 변수
extern web_object *root;
extern web_object *tail;
extern int total_cache_size;
