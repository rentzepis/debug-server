#include <stdio.h>
#include "csapp.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// Use doubly linked list to simulate each cache set
typedef struct dataNode{
	struct dataNode *prev;
	struct dataNode *next;
	char *url;
	char *data;
	int dataLength;
} dataNode;

dataNode* saveData(char* url, char* data, int dataLength);
dataNode* getData(char* url);
void eviction(int dataSize);
int deleteNodeToHead(dataNode* newData);
void deleteNode(dataNode* newData);
void freeNode(dataNode* newData);
int test_cache();
