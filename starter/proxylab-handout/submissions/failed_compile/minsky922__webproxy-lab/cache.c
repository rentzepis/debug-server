#include "cache.h" 
c_list *init_cache_list(){
	c_list *list = Malloc(sizeof(*list));
	list->head = list->tail = NULL;
	list->lock = Malloc(sizeof(*(list->lock)));
	pthread_rwlock_init((list->lock), NULL);
	list->bytes_left = MAX_CACHE_SIZE;

	return list;
}
void init_node(c_node *node){
	if(node) {
		node->next = NULL;
		node->prev = NULL;
		node->length = 0;
	}
}

void set_node(c_node *node, char *index, unsigned int len){
	if(node) {
		node->index = Malloc(sizeof(char)*len);
		memcpy(node->index, index,len);
		node->length = len;
	}
}

void delete_node(c_node *node){
	if(node){
		if(node->index)
			free(node->index);
		if(node->content)
			free(node->content);
		free(node);
	}
}

void delete_list(c_list *list) {
	if(list) {
		c_node *tmp = list->head;
		while(tmp) {
			c_node *tmp2 = tmp;
			tmp = tmp2->next;
			delete_node(tmp2);
		}
		
		pthread_rwlock_destroy((list->lock));
		free(list->lock);
		free(list);
	}
}

c_node *search_node(c_list *list, char *index) {
	if(list) {
		c_node *tmp = list->head;
		while(tmp) {
			if(!strcmp(tmp->index, index))
				return tmp;
			tmp = tmp->next;
		}
	}
	return NULL;
}

void add_node(c_node *node, c_list *list){
	if(list) {
		//pthread_rwlock_wrlock((list->lock));
		if(node){
			while(list->bytes_left < node->length) {
				c_node *tmp_node = evict_list(list);
				delete_node(tmp_node);
			}
			if(!list->tail) {
				list->head = list->tail = node;
				list->bytes_left -= node->length;
			}
			else {
				list->tail->next = node;
				node->prev = list->tail;
				list->tail = node;
				list->bytes_left -= node->length;
			}
		}
		//pthread_rwlock_unlock((list->lock));
	}
}

c_node *remove_node(char *index, c_list *list){
	if(list){
		//pthread_rwlock_wrlock((list->lock));
		c_node *tmp = search_node(list, index);
		if(tmp) {
			if(tmp == list->head)
				tmp = evict_list(list);
			else {
				if(tmp->prev) 
					tmp->prev->next = tmp->next;
				if(tmp->next)
					tmp->next->prev = tmp->prev;
				else
					list->tail = tmp->prev;
				list->bytes_left += tmp->length;
			}
			tmp->prev = NULL;
			tmp->next = NULL;
		}
		//pthread_rwlock_unlock((list->lock));
		return tmp;
	}
	
	return NULL;
}

c_node *evict_list(c_list *list){
	if(list){
		c_node *tmp = list->head;
		if(tmp) {
			if(tmp->next)
				tmp->next->prev = NULL;
			list->bytes_left += tmp->length;
			if(list->head == list->tail)
				list->tail = NULL;
			list->head = tmp->next;
			return tmp;
		}
	}
	return NULL;
}

int read_node_content(c_list *list, char *index, char *content, unsigned int *len){
	if(!list)
		return -1;
	
	pthread_rwlock_rdlock((list->lock));
	
	c_node *tmp = search_node(list, index);
	
	if(!tmp)
	{
		pthread_rwlock_unlock((list->lock));
		return -1;
	}
	
	*len = tmp->length;
	memcpy(content, tmp->content,*len);
	
	pthread_rwlock_unlock((list->lock));
	
	pthread_rwlock_wrlock((list->lock));
	add_node(remove_node(index, list), list);
	pthread_rwlock_unlock((list->lock));
	
	return 0;
}

int insert_content_node(c_list *list, char *index, char *content, unsigned int len){
	if(!list)
		return -1;
	
	c_node *tmp = Malloc(sizeof(*tmp));
	init_node(tmp);
	set_node(tmp, index, len);
	
	if(!tmp)
		return -1;
		
	tmp->content = Malloc(sizeof(char)*len);
	memcpy(tmp->content, content,len);

	pthread_rwlock_wrlock((list->lock));
	add_node(tmp, list);
	pthread_rwlock_unlock((list->lock));
	return 0;
}