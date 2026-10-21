/*
 *            Name: Kangping Yao; Andrew ID: kyao
 * I implemented this cache with a doubly linked list. 
 * Every time we get data by searching the list from head, and
 * If we find data, put this data node to the head.
 * Before save data, we need to check whether the remaining space
 * is big enough. If it is, put the data node to the head, and if
 * not, we need to do eviction which is searching the list from the
 * tail to find the first fit. If we can not find such node, we delete
 * node from tail one by one util we get enough space.
 */
#include "cache.h"
static dataNode* head = NULL;
static dataNode* tail = NULL;
static uint32_t remaining = MAX_CACHE_SIZE;

//search data from cache by url
dataNode* getData(char* url){
	dataNode* currentData = head;
	while(currentData != NULL){
		if(!strcmp(url, currentData->url)){
			deleteNodeToHead(currentData);
			return currentData;
		}
		currentData = currentData -> next;
	}
	return NULL;
}

//save data to cache, and move new data to head
dataNode* saveData(char* url, char* data, int dataLength){
	dataNode *newData;
	newData=(dataNode*) malloc(sizeof(dataNode));
	newData->url = (char*)Malloc(strlen(url)+1);
	strcpy(newData->url, url);
	newData->data = (char*)Malloc(dataLength);
	memcpy(newData->data, data, dataLength);
	newData->dataLength = dataLength;
	//If the cache space isn't enough, do eviction
	if(dataLength > remaining){
		eviction(dataLength);
	}
	remaining -= dataLength;
	//put new data to head
	newData -> prev = NULL;
	newData -> next = NULL;
	if(head == NULL){
		head = newData;
		tail = newData;
	}else{
		newData -> next = head;
		head -> prev = newData;
		head = newData;
	}
	return newData;
}

//delete node from original list and insert it to head
int deleteNodeToHead(dataNode* newData){
	if(newData->prev == NULL){
		//already at head
		return 1;
	}else if(newData->next == NULL){
		newData -> prev -> next = NULL;
		tail = newData -> prev;
		newData -> prev = NULL;
		newData -> next = head;
		head -> prev = newData;
		head = newData;
		return 1;
	}else{
		//node in middle place, delete node first
		newData -> prev -> next = newData -> next;
		newData -> next -> prev = newData -> prev;
		//add data node to head
		newData -> prev = NULL;
		newData -> next = head;
		head -> prev = newData;
		head = newData;
		return 1;
	}
	return -1;
}
//eviction for more data space to store newest data
void eviction(int dataSize){
	dataNode *dataPtr = tail;
	//try to find first fit
	while(dataPtr != NULL){
		if(dataPtr->dataLength > dataSize){
			remaining += dataPtr->dataLength;
			deleteNode(dataPtr);
			return;
		}
		dataPtr = dataPtr -> prev;
	}
	//if not found, delete from tail one by one
	while(remaining < dataSize){
		dataPtr = tail;
		remaining += dataPtr->dataLength;
		deleteNode(dataPtr);
	}
	return;
}

//Delete a node from list
void deleteNode(dataNode* node){
	if(node -> prev == NULL){
		if(node -> next == NULL){
			head = NULL;
			tail = NULL;
		}else{
			head = node -> next;
			head -> prev =NULL;
		}
	}else{
		if(node -> next == NULL){
			node -> prev -> next = NULL;
			tail = node -> prev;
		}else{
			node -> prev -> next = node -> next;
			node -> next -> prev = node -> prev;
		}
	}
	freeNode(node);
}

//Free the memory of the deleted node
void freeNode(dataNode* node){
	Free(node-> url);
	Free(node-> data);
	Free(node);
}

