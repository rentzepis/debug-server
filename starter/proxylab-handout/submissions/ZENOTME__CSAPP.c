#define DEBUG
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

typedef struct
{
	char method[10];
	char url[512];
	char version[10];
	char host[256];
	char port[20];
	char uri[256];
}t_hostbox;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *conn_hdr="Connection: close\r\n";
static const char *prox_hdr="Proxy-Connection: close\r\n";
/*Function*/
void *thread(void *vargp);
static void doit(int connfd);
/**********************/
static void url_parse(int connfd,t_hostbox* hostbox);
static void url_send(int connfd,t_hostbox* hostbox);
/**********************/
void clienterror(int fd,char *cause,char *errnum,char *shortmsg,char *longmsg);
//=====================================================================
void sigpipe_handler(int sig)
{
	printf("%ld Sigpipe handled %d\n",pthread_self(),sig);
	pthread_exit(0);
	return;
}

void sigsegv_handler(int sig)
{
	printf("%ld Sigsegv handled %d\n",pthread_self(),sig);
	pthread_exit(0);
	return;
}

int main(int argc,char **argv)
{	
	int listenfd,*connfd;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen;
	pthread_t tid;
	char client_hostname[MAXLINE],client_port[MAXLINE];
	
	Signal(SIGPIPE,sigpipe_handler);
	Signal(SIGSEGV,sigsegv_handler);

	if(argc!=2)
	{
		fprintf(stderr,"usage: %s <port>\n",argv[0]);
		exit(0);
	}
	listenfd=Open_listenfd(argv[1]);
	while(1)
	{
		clientlen=sizeof(clientaddr);
		connfd=Malloc(sizeof(int));
		*connfd=Accept(listenfd,(SA*)&clientaddr,&clientlen);
		Getnameinfo((SA *)&clientaddr,clientlen,client_hostname,MAXLINE,client_port,MAXLINE,0);
		printf("accept connection from (%s %s)\n",client_hostname,client_port);
		Pthread_create(&tid,NULL,thread,connfd);
	}
}



void *thread(void *vargp)
{
	int connfd=*((int*)vargp);
	Pthread_detach(pthread_self());
	Free(vargp);
	doit(connfd);
	Close(connfd);
	return NULL;
}

//===========================================================

static void doit(int connfd)
{
	t_hostbox hostbox;
	url_parse(connfd,&hostbox);
#ifdef DEBUG
printf("%s %s %s\n",hostbox.method,hostbox.url,hostbox.version);
printf("URL:%s\n",hostbox.url);
printf("HOST:%s\n",hostbox.host);
printf("PORT:%s\n",hostbox.port);
printf("URI:%s\n",hostbox.uri);
#endif
	url_send(connfd,&hostbox);
}

static void url_parse(int connfd,t_hostbox *hostbox)
{
	rio_t rio;
	char t_buf[MAXLINE],t_method[10],t_url[1024],t_version[20];
	char t_host[256];
	char t_uri[256];
	char *ptr1;
	char *ptr2;
	int t_port=80;

	Rio_readinitb(&rio,connfd);
	Rio_readlineb(&rio,t_buf,MAXLINE);
	
	int *p;
	*(p)=0;
	sscanf(t_buf,"%s %s %s",t_method,t_url,t_version);
	strcpy(hostbox->version,t_version);
	strcpy(hostbox->method,t_method);
	strcpy(hostbox->url,t_url);
	ptr1=strstr(t_url,"//");
	ptr1=(ptr1==NULL?t_url:ptr1+2);
	if((ptr2=strstr(ptr1,":")))
	{
		*ptr2='\0';
		sscanf(ptr1,"%s",t_host);
		sscanf(ptr2+1,"%d%s",&t_port,t_uri);
		if(!strcmp(t_uri,""))
		  strcpy(t_uri,"/");
	}
	else
	{
		if((ptr2=strstr(ptr1,"/")))
		{
			strcpy(t_uri,ptr2);
			strncpy(t_host,ptr1,ptr2-ptr1);
		}
		else
		{
			strcpy(t_host,ptr1);
			strcpy(t_uri,"/");
		}
	}
	sprintf(hostbox->port,"%d",t_port);
	strcpy(hostbox->host,t_host);
	strcpy(hostbox->uri,t_uri);
}

static void url_send(int connfd,t_hostbox* hostbox)
{
	int clientfd;
	rio_t rio;
	char buf[MAXLINE];
    if((clientfd=Open_clientfd(hostbox->host,hostbox->port))==-1)
	{
		clienterror(connfd,"URL","404","Can't connect","Proxy can't connect the url");
		printf("%d error:I can't connect the url:Thread destory!\n",connfd);
		pthread_exit(0);
	}
	printf("%d Sucessful Connect Servive....\n",connfd);
	Rio_readinitb(&rio,clientfd);
/********Send request*********/
	strcpy(buf,hostbox->method);
	strcat(buf," ");
	strcat(buf,hostbox->uri);
	strcat(buf," HTTP/1.0\r\n");
	Rio_writen(clientfd,buf,strlen(buf));

	strcpy(buf,"Host:");
	strcat(buf,hostbox->host);
	strcat(buf,"\r\n");
	Rio_writen(clientfd,buf,strlen(buf));

	strcpy(buf,user_agent_hdr);
	Rio_writen(clientfd,buf,strlen(buf));
	
	strcpy(buf,conn_hdr);
	Rio_writen(clientfd,buf,strlen(buf));

	strcpy(buf,prox_hdr);
	Rio_writen(clientfd,buf,strlen(buf));

	Rio_writen(clientfd,"\r\n",3);
/*******************************/
	int n=0;
	int m=0;
	while((n=Rio_readnb(&rio,buf,MAXLINE))!=0)
	{
		m+=n;
		Rio_writen(connfd,buf,n);
		printf("%d send %d bytes..\n",connfd,n);
	}
	printf("%d totally send %d bytes\n",connfd,m);
}



void clienterror(int fd,char *cause,char *errnum,char *shortmsg,char *longmsg)
{
	char buf[MAXLINE],body[MAXBUF];

	/*Build the HTTP responese body*/
	sprintf(body,"<html><title>Proxy Error</title>");
	sprintf(body,"%s<body bgcolor=""ffffff"">\r\n",body);
	sprintf(body,"%s%s:%s\r\n",body,errnum,shortmsg);
	sprintf(body,"%s<p>%s: %s\r\n",body,longmsg,cause);
	sprintf(body,"%s<hr><em>The Tiny Web server</em>\r\n",body);

	/*Print the HTTP response*/
	sprintf(buf,"HTTP/1.0 %s %s\r\n",errnum,shortmsg);
	Rio_writen(fd,buf,strlen(buf));
	sprintf(buf,"Conten-type: text/html\r\n");
	Rio_writen(fd,buf,strlen(buf));
	sprintf(buf,"Conten-length:%d\r\n\r\n",(int)strlen(body));
	Rio_writen(fd,buf,strlen(buf));
	Rio_writen(fd,body,strlen(body));
}

