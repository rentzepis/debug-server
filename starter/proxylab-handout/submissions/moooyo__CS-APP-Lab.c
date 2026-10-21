#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define 	MAX_TYPE_SIZE   	4
#define MAX_THREAD_SIZE 	8
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *user_connection="Connection: close\r\n";
static const char *user_proxy_connection="Proxy-Connection: close\r\n";
typedef struct {
	char user_agent_hdr[MAXLINE];
	char user_connection[MAXLINE];
	char user_proxy_connection[MAXLINE];
	char user_host[MAXLINE];
}Headers;

static const int MAX_CACHE_NUM=MAX_CACHE_SIZE/MAX_OBJECT_SIZE;

/*
 * Subf in textbook p705
 */
typedef struct{
    int *buf;
    int n;
    int front;
    int rear;
    sem_t mutex;
    sem_t slots;
    sem_t items;
}sbuf_t;
void sbuf_init(sbuf_t *sp, int n);
void sbuf_deinit(sbuf_t *sp);
void sbuf_insert(sbuf_t *sp,int item);
int sbuf_remove(sbuf_t *sp);
/*
 * Cache struct
 */
typedef struct
{
	char url[MAXLINE];
	char cache[MAX_OBJECT_SIZE];
	time_t useTime;
	int size;
	sem_t wt,rd,mutex;
	int readcnt;
}CacheItem;
typedef struct 
{
	CacheItem *cacheItem;
	int usedCacheNum;
	int cacheNum;
	sem_t nt;
}Cache;


void* todo(void *vargp);
void PrintErrorHeader(int fd,char status[],char* shortMsg);
void ServerError(int fd, char *cause, char status[], char *shortMsg, char *longMsg);
void ServerPrintResponseHeader(char *header);
int getRequestType(char *buf);
void initHeader(Headers* header);
void ParseUrl(char url[],char uri[],char hostname[],char port[],Headers* header);
void GetRequestHeaders(rio_t *rio,char requestHeaders[],Headers *header);
void initCache(Cache *cache,int n);
int searchCache(Cache *cache,char  *url);//return -1 if don't find
sbuf_t sbuf;
Cache cache;
int main(int argc,char **argv)
{
    //printf("%s", user_agent_hdr);
    int listenfd,connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char hostname[MAXLINE],port[MAXLINE];
    pthread_t tid;
    if(argc!=2)
    {
    	fprintf(stderr,"usage :%s <port> \n",argv[0]);
    	exit(0);
    }
    initCache(&cache,MAX_CACHE_NUM);

    Sem_init(&cache.nt,0,1);
    listenfd=Open_listenfd(argv[1]);
    sbuf_init(&sbuf,MAX_THREAD_SIZE);
    
    for(int i=0;i<MAX_THREAD_SIZE;++i)
    {
    		Pthread_create(&tid,NULL,todo,NULL);
    }
    while(1)
    {
    	clientlen=sizeof(struct sockaddr_storage);
    	connfd=Accept(listenfd,(SA *)&clientaddr,&clientlen);
    	Getnameinfo((SA *)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
    	fprintf(stdout, "Accept: (%s,%s)\n",hostname,port);
    	sbuf_insert(&sbuf,connfd);
    }
    return 0;
}
void* todo(void *vargp)
{
	rio_t rio,rioc;
	int rc;
	char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE],url[MAXLINE];
	char hostname[MAXLINE],port[MAXLINE],temp[MAXLINE];
	char cacheBuf[MAX_OBJECT_SIZE];

	Pthread_detach(pthread_self());
	int fd=sbuf_remove(&sbuf);
	//fprintf(stdout, "Get fd:%d\n",fd );
	Rio_readinitb(&rio,fd);
	Rio_readlineb(&rio,buf,MAXLINE);
 	//fprintf(stdout,"Proxy recive header:\n%s\n",buf);
	sscanf(buf,"%s %s %s",method,url,version);
	if(strcasecmp(method,"GET"))
	{
		ServerError(fd,method,"501","Not implemented","ProxyLab not implement this method\n");
	}

	int ci=searchCache(&cache,url);
	if(ci==-1){
		fprintf(stdout,"ProxyCache: %s don't Cached\n",url);
	  	Headers header;
	  	ParseUrl(url,uri,hostname,port,&header);
		sprintf(buf,"%s %s %s",method,uri,"HTTP/1.0\r\n");
		GetRequestHeaders(&rio,buf,&header);
		sscanf(header.user_host,"%s %s",temp,hostname);
		char *ptr=strchr(hostname,':');
		if(ptr!=NULL){
			strcpy(port,ptr+1);
			*ptr='\0';
		}else
		{
			strcpy(port,"80");
		}
	//  fprintf(stdout,"Open_clientfd:hostname=%s port=%s\n",hostname,port);
		int connfd=Open_clientfd(hostname,port);
		if(connfd<0)
		{
			ServerError(fd,method,"500","Can not Connection","Proxy Can't connection to end server\n");
		}
	//  fprintf(stdout,"Proxy will send header:\n%s\n",buf);
		Rio_writen(connfd,buf,MAXLINE);
		Rio_readinitb(&rioc,connfd);
		int bufCnt=0;
		memset(buf,0,sizeof(buf));
		memset(cacheBuf,0,sizeof(cacheBuf));
		while((rc=Rio_readnb(&rioc,buf,MAXLINE))>0)
		{
			bufCnt+=rc;
	 //   	fprintf(stdout,"Proxy received %d bytes,ten send to end_server\n",rc);
	    	if(bufCnt<MAX_OBJECT_SIZE)
	    	{
	    		strcat(cacheBuf,buf);
	    	}
			Rio_writen(fd,buf,rc);
		}
		fprintf(stdout,"ProxyCacheBuf:size=%d\n%s\n",bufCnt,buf);
		if(bufCnt<MAX_OBJECT_SIZE)
		{
			//cache it
			int rti,usedCacheNum;

			P(&cache.nt);
			usedCacheNum=cache.usedCacheNum;
			if(usedCacheNum<MAX_CACHE_NUM)
			{
				cache.usedCacheNum++;
			}
			V(&cache.nt);

			if(usedCacheNum<cache.cacheNum)
			{
				for(int i=0;i<cache.cacheNum;++i)
				{
					P(&cache.cacheItem[i].wt);
					if(cache.cacheItem[i].useTime==0)
					{
						rti=i;
						V(&cache.cacheItem[i].wt);
						break;
					}
					V(&cache.cacheItem[i].wt);

				}
			}else
			{
				time_t LRU_TIME=time(NULL)+1;
				for(int i=0;i<cache.cacheNum;++i)
				{
					P(&cache.cacheItem[i].wt);
					if(cache.cacheItem[i].useTime<LRU_TIME)
					{
						LRU_TIME=cache.cacheItem[i].useTime;
						rti=i;
					}
					V(&cache.cacheItem[i].wt);
				}
			}

			P(&cache.cacheItem[rti].wt);
			strcpy(cache.cacheItem[rti].cache,cacheBuf);
			strcpy(cache.cacheItem[rti].url,url);
			cache.cacheItem[rti].size=bufCnt;
			cache.cacheItem[rti].useTime=time(NULL);
			V(&cache.cacheItem[rti].wt);
		}

	 	Close(connfd);
	}else
	{
		P(&cache.cacheItem[ci].mutex);
		cache.cacheItem[ci].readcnt++;
		if(cache.cacheItem[ci].readcnt==1)
		{
			P(&cache.cacheItem[ci].wt);
		}
		V(&cache.cacheItem[ci].mutex);

		fprintf(stdout,"ProxyCache: %s  Cached\n",url);
		fprintf(stdout,"ProxyCacheBuf:size=%d\n%s\n",cache.cacheItem[ci].size,cache.cacheItem[ci].cache);
		Rio_writen(fd,cache.cacheItem[ci].cache,cache.cacheItem[ci].size);
		cache.cacheItem[ci].useTime=time(NULL);
		P(&cache.cacheItem[ci].mutex);
		cache.cacheItem[ci].readcnt--;
		if(cache.cacheItem[ci].readcnt==0)
		{
				V(&cache.cacheItem[ci].wt);
		}
		V(&cache.cacheItem[ci].mutex);
	}
  Close(fd);
}
int getRequestType(char *buf)
{
	const static char type[MAX_TYPE_SIZE][MAXLINE]={"Host:","User-Agent:","Connection:","Proxy-Connection:"};
	int i;
  for(i=0;i<MAX_TYPE_SIZE;++i)
	{
		if(strcasecmp(buf,type[i])==0)
		{
			return i;
		}
	}
	return -1;
}
void ParseUrl(char urltemp[],char uri[],char hostname[],char port[],Headers* header)
{
	char url[MAXLINE];
	strcpy(url,urltemp);
  char *ptr=strchr(url,':');
  char *uri_ptr=strchr(ptr+3,'/');
  strcpy(uri,uri_ptr);
  *uri_ptr='\0';
  strcpy(header->user_host,ptr+2);
  char *port_ptr=strchr(ptr+3,':');
  if(port_ptr==NULL)
    {
      strcpy(port,"80");
    }else{
      strcpy(port,port_ptr+1);
      *port_ptr='\0';
   }
  strcpy(hostname,ptr+3);
}
void initHeader(Headers* header)
{
	strcpy(header->user_agent_hdr,user_agent_hdr);
	strcpy(header->user_connection,user_connection);
	strcpy(header->user_proxy_connection,user_proxy_connection);
}
void GetRequestHeaders(rio_t *rio,char requestHeaders[],Headers *header)
{
	char buf[MAXLINE],type[MAXLINE];
	int rc,tc;
	initHeader(header);
	while((rc=Rio_readlineb(rio,buf,MAXLINE))>0)
	{
		if(strcmp(buf,"\r\n")==0)
		{
			break;
		}
		sscanf(buf,"%s",type);
		tc=getRequestType(type);
		//fprintf(stdout, "%s",buf );
		switch(tc)
		{
			case -1:strcat(requestHeaders,buf);break;
			case 0:strcpy(header->user_host,buf);
			case 1:
			case 2:
			case 3:
				break;
		}
	}
	strcat(requestHeaders,header->user_agent_hdr);
	strcat(requestHeaders,header->user_connection);
	strcat(requestHeaders,header->user_proxy_connection);
	strcat(requestHeaders,header->user_host);
	strcat(requestHeaders,"\r\n");
}


/*
 * help function
 */

void PrintErrorHeader(int fd,char status[],char* shortMsg)
{
    char buf[MAXBUF];
    /*Send the HTTP response Header to client*/
    sprintf(buf, "HTTP/1.0 %s %s\r\n",status,shortMsg);
    sprintf(buf, "%sServer: Yu Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n\r\n", buf);

    /*send header to client && log*/
    rio_writen(fd, buf, strlen(buf));
    ServerPrintResponseHeader(buf);
}
void ServerPrintResponseHeader(char *header) {
    printf("Response headers:\n");
    printf("%s", header);
}
void ServerError(int fd, char *cause, char status[], char *shortMsg, char *longMsg) {
    PrintErrorHeader(fd,status,shortMsg);
}

/*
 * sbuf function
 */
void sbuf_init(sbuf_t *sp,int n)
{
 	sp->buf=malloc(sizeof(int)*n);
 	sp->n=n;
 	sp->front=sp->rear=0;
 	Sem_init(&sp->mutex,0,1);
 	Sem_init(&sp->slots,0,n);
 	Sem_init(&sp->items,0,0);
}
void sbuf_deinit(sbuf_t *sp)
{
	Free(sp->buf);
}
void sbuf_insert(sbuf_t *sp, int item)
{
	P(&sp->slots);
	P(&sp->mutex);
	sp->buf[(++sp->rear)%(sp->n)]=item;
	V(&sp->mutex);
	V(&sp->items);
}
int sbuf_remove(sbuf_t *sp)
{
	int item;
	P(&sp->items);
	P(&sp->mutex);
	item=sp->buf[(++sp->front)%(sp->n)];
	V(&sp->mutex);
	V(&sp->slots);
	return item;
}

void initCache(Cache *cache,int n)
{
	cache->cacheNum=n;
	cache->cacheItem=malloc(sizeof(CacheItem)*n);
	cache->usedCacheNum=0;
	for(int i=0;i<n;++i)
	{
		cache->cacheItem[i].useTime=0;
		cache->cacheItem[i].readcnt=0;
		Sem_init(&cache->cacheItem[i].wt,0,1);
		Sem_init(&cache->cacheItem[i].rd,0,1);
		Sem_init(&cache->cacheItem[i].mutex,0,1);
	}
}

int searchCache(Cache *cache,char *url)
{
	for(int i=0;i<MAX_CACHE_NUM;++i)
	{
		if(strcmp(url,cache->cacheItem[i].url)==0)
		{
			return i;
		}
	}
	return -1;
}