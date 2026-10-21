#include "cache.h"
#include <stdlib.h>
#include <string.h>

void init()
{
    sum_of_cache = 0;
    rear = (front = NULL);
}

cnode *find(char *uri)
{
    cnode *temp = front;

    for(temp = front; temp != NULL; temp = temp->next)
    {
        printf("uri: %s \n THIS IS CACHE", temp-> uri);
        if(!strcmp(temp->uri, uri))
        {
            if(temp == front)
            {
                if(temp->next != NULL)  temp->next->prev = NULL;
                if(rear != NULL)    rear->next = temp;
                temp->prev = rear;
                rear = temp;
                front = temp->next;
                temp->next = NULL;
            }
            else if(temp == rear);
            else
            {
                temp->prev->next = temp->next;
                temp->next->prev = temp->prev;
                temp->prev = rear;
                temp->prev->next = temp;
                rear = temp;
            }
            return temp;
        }
    }
    return NULL;
}

void replacement(int length)
{
    cnode *temp;

	while(front != NULL && sum_of_cache + length > MAX_CACHE_SIZE) {
		sum_of_cache -= front->content_size;
		temp = front->next;
		Free(front);
		front = temp;
	}

	if(front == NULL) rear = NULL;
}

void add_cache(char *uri, char *content)
{
    cnode *new_block;

	if(strlen(content) > MAX_OBJECT_SIZE) return;

	if(find(uri) != NULL) return;

	replacement(strlen(content));

	new_block = (cnode *) Malloc(sizeof(cnode));

	strncpy(new_block->uri, uri, strlen(uri) + 1);
	memcpy(new_block->object, content, strlen(content));
	new_block->content_size = strlen(content);
	new_block->next = NULL;
    new_block->prev = NULL;
	
	sum_of_cache += strlen(content);
	
	if(rear == NULL) front = (rear = new_block);
	else {
		rear->next = new_block;
        new_block->prev = rear;
		rear = new_block;
	}

    printf("Cache Addition Finished");
    for(cnode *t = front; t!=NULL; t= t->next)
    {
        printf("%s\n", t->uri);
    }
}