#include "csapp.h"
#include "cache.h"
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
/*******cache*********/


void cache_init(cache_t* cache)
{
	cache->n=0;
	cache->read_cnt=0;
	cache->sum_length=0;
	cache->start=NULL;
	cache->end=NULL;
	Sem_init(&cache->s_write,0,1);
	Sem_init(&cache->s_mux,0,1);
}

char* cache_read(cache_t* cache,char *url)
{
	cache_node_t* node=NULL;
	cache_node_t* rnode=NULL;
	char* buf=NULL;

	P(&cache->s_mux);
	cache->read_cnt++;
	if(cache->read_cnt==1)
	  P(&cache->s_write);
	V(&cache->s_mux);

	for(node=cache->start;node!=NULL;node=node->next)
	{
		if(!strcmp(node->name,url))
		{
			buf=node->buf;
			rnode=node;
			continue;
		}
	}
	if(rnode!=NULL)
	{

		P(&cache->s_mux);
		if(rnode==(cache->end)){}
		else if(rnode==(cache->start))
		{
				cache->start=rnode->next;
				((rnode->next)->last)=NULL;

				rnode->next=NULL;
				(cache->end)->next=rnode;
				rnode->last=cache->end;
				cache->end=rnode;
		}
		else
		{
				(rnode->last)->next=(rnode->next);
				(rnode->next)->last=(rnode->last);
				rnode->next=NULL;
				rnode->last=cache->end;
				(cache->end)->next=rnode;
				cache->end=rnode;
		}
		V(&cache->s_mux);
	}

	P(&cache->s_mux);
	cache->read_cnt--;
	if(cache->read_cnt==0)
	  V(&cache->s_write);
	V(&cache->s_mux);

	return buf;
}

void cache_write(cache_t *cache,char *buf,char *url,int len)
{
		cache_node_t *code;

		P(&cache->s_write);
		/**********************/
		if((cache->sum_length+len)<=MAX_CACHE_SIZE)
		{
			cache_node_t *node=(cache_node_t*)malloc(sizeof(cache_node_t));
		
			node->name=(char*)Calloc(sizeof(char),(strlen(url)+1));
			strcpy(node->name,url);
			node->length=len;
			node->buf=(char*)Calloc(sizeof(char),len);
			strcpy(node->buf,buf);
			node->next=NULL;
			node->last=cache->end;
		
			if(cache->n==0)
			{
				cache->end=node;
				cache->start=node;
			}
			else
			{
				(cache->end)->next=node;
				cache->end=node;
			}
			cache->n++;
			cache->sum_length+=len;
		}
		else
		{
			int i=0;
			int sum_size=0;
			int need_len=len-(MAX_CACHE_SIZE-cache->sum_length);
			for(code=cache->start;code!=NULL;code=code->next)
			{
				i++;
				sum_size+=code->length;
				if(sum_size>=need_len)
				  break;
			}
			code=cache->start;
			for(int j=0;j<i;j++)
			{
				cache_node_t *t_code=code;
				code=code->next;
				free(t_code);
				cache->n--;
			}
			code->last=NULL;
			cache->start=code;
			cache->sum_length-=sum_size;
			/*****************************************/
			cache_node_t *node=(cache_node_t*)malloc(sizeof(cache_node_t));
			
			node->name=(char*)Calloc(sizeof(char),strlen(url+1));
			strcpy(node->name,url);
			
			node->length=len;
			node->buf=(char *)Calloc(sizeof(char),len);
			strcpy(node->buf,buf);

			node->next=NULL;
			node->last=cache->end;
	
			if(cache->n==0)
			{
				cache->end=node;
				cache->start=node;
			}
			else
			{
				(cache->end)->next=node;
				cache->end=node;
			}
			cache->n++;
			cache->sum_length+=len;
		}
		/*********************/
		V(&cache->s_write);
}

void cahce_free(cache_t* cache)
{
	cache_node_t* node=cache->start;
	for(int i=0;i<cache->n;i++)
	{
		free(node->name);
		free(node->buf);
		node=node->next;
		if(node!=cache->end)
		{
			node=node->next;
			free(node->last);
		}
		else
		{
			free(node);
		}
	}
}
