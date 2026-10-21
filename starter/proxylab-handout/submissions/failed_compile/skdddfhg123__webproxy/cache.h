#ifndef __CACHE_H__
#define __CACHE_H__

#include "csapp.h"

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct c_node {
	char *index;
	char *content;
	struct c_node *prev;
	struct c_node *next;
	unsigned int length;
} c_node;

typedef struct c_list {
	c_node *head;
	c_node *tail;
	pthread_rwlock_t *lock;
	unsigned int bytes_left;
} c_list;

c_list *init_cache_list();
void init_node(c_node *node);

void set_node(c_node *node, char *index, unsigned int len);
void delete_node(c_node *node);
void delete_list(c_list *list);

c_node *search_node(c_list *list, char *index);
void add_node(c_node *node, c_list *list);
c_node *remove_node(char *index, c_list *list);
c_node *evict_list(c_list *list);

int read_node_content(c_list *list, char *index, char *content, unsigned int *len);
int insert_content_node(c_list *list, char *index, char *content, unsigned int len);


#endif
