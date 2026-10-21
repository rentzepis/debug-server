#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

int sum_of_cache;

typedef struct cnode {
	char uri[MAXLINE];
	char object[MAX_OBJECT_SIZE];
	int content_size;
	struct cnode *next;
    struct cnode *prev;
} cnode;

cnode *rear, *front;

void init();
cnode *find(char *uri);
void replacement(int length);
void add_cache(char *uri,  char *content);