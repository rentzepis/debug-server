#include "cache.h"
#include "csapp.h"
#include "cache.h"

obj_t* least_recently_used;
obj_t* most_recently_used;
size_t cache_memory_used;
sem_t mutex, s;
int number_of_obj;
int isreading ;

void cache_init(){
    least_recently_used = NULL;
    most_recently_used = NULL;
    cache_memory_used = 0;
    number_of_obj = 0;
    isreading = 0;
    Sem_init(&mutex,0,1);
    Sem_init(&s,0,1);
}


/*
    Delete a node of the LRU list
*/
void deletee(obj_t *obj){
    if(most_recently_used == least_recently_used){ //If it's the same object
        most_recently_used = NULL;
        least_recently_used = NULL;
    }
    else if(obj->prev == NULL){ //If it was the least recently used
        least_recently_used = obj->next;
        obj->next->prev = NULL;
    }
    else if(obj->next == NULL) { // If it was the most recently used
        most_recently_used = obj->prev;
        obj->prev->next = NULL;
    }
    else{ // Regular case
        obj->prev->next = obj->next;
        obj->next->prev = obj->prev;
    }
    number_of_obj--;
    cache_memory_used -= obj->size;
}

void push(obj_t* obj){
    //If it's the first object
    if(number_of_obj == 0){
        least_recently_used = obj;
        most_recently_used = obj;
        obj->next = NULL;
        obj->prev = NULL;
    }
    //else we push the object on the top of the list
    else{
        most_recently_used->next = obj;
        obj->prev = most_recently_used;
        obj->next = NULL;
        most_recently_used = obj;
    }
    number_of_obj++;
    cache_memory_used += obj->size;

}
//We evict the least recently used
void evict(){
    obj_t* obj_evicted;

    //no eviction possible if there is no object
    if(number_of_obj == 0){
        return;
    }
    //if it equals to 1, this is the same as if we have an empty cache
    else if(number_of_obj == 1){
        obj_evicted = least_recently_used;
        most_recently_used = NULL;
        least_recently_used = NULL;
    }
    else{
        obj_evicted = least_recently_used;
        obj_evicted->next->prev = NULL;
        least_recently_used = obj_evicted->next;
    }
    cache_memory_used = cache_memory_used - obj_evicted->size;
    number_of_obj--;
    myfree(obj_evicted->heads, obj_evicted);



}

//Constructor of an object obj
obj_t* create_obj(headers_t *heads, size_t size, char* tosend){
    obj_t* obj =(obj_t*) Malloc(sizeof(obj_t*)*10000000); //10000000 because otherwise not enough space could be better by calculating the size, waste of memory
    obj->heads = heads;
    obj->tosend = Malloc(sizeof(char)*(strlen(tosend)+1));
    strncpy(obj->tosend,tosend,(strlen(tosend)+1));
    obj->size = size;

    return obj;

}

//Find an object in the cache
obj_t* find_in_cache(headers_t* heads){
    obj_t* sol = most_recently_used;
    while(sol!=NULL){
        if(isEqualObj(heads,sol)){
            return sol;
        }
        sol = sol->prev;
    }
    return NULL;
}

//The next two functions compare one object to another
int isEqualObj(headers_t* heads, obj_t* obj){
    return isEqualHead(heads, obj->heads);
}

int isEqualHead(headers_t* heads, headers_t* heads2){
    //We need to check only those three, since the rest is supposed to be the same in our case
    if(strcmp(heads->host, heads2->host))
        return 0;
    if(strcmp(heads->path, heads2->path))
        return 0;
    if(strcmp(heads->portnumber, heads2->portnumber))
        return 0;
    return 1;
}

void myfree(headers_t* heads, obj_t* obj){
    free(heads->connection);
    free(heads->host);
    free(heads->path);
    free(heads->portnumber);
    free(heads->proxy_connection);
    free(heads->type);
    free(heads);
    free(obj);
    
}