#include "csapp.h"
#include "http.h"
#include <time.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHEL 100

void* doit(void* pt1);
void send_requesthdrs(rio_t *rp, int file, char* host);
int parse_website(char **website, char **filename, char **host);
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg);


typedef struct
{
	int connfd;
	pthread_t thread;
} pt_helper;

//pointers to the pages in the cache
void* pages[CACHEL];
//timestamps for when the pages were last accessed
time_t times[CACHEL];
//requests for websites, used to identify if the page is correct
char* names[CACHEL];
//sizes of all the stuff
size_t sizes[CACHEL];
//size of all the objects in the cache
int cachesize = 0;

long counter = 0;

sem_t mut,  writeLock;
int readLock = 0;

int main(int argc, char **argv) 
{
    int listenfd, port, clientlen;
    struct sockaddr_in clientaddr;
//	int* connfd;

    /* Check command line args */
    if (argc != 2)
	{
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(1);
    }
	
	signal(SIGPIPE, SIG_IGN);
	
	port = atoi(argv[1]);
	
	sem_init(&mut, 0, 1);	
	sem_init(&writeLock, 0, 1);
	

	//initialize globals
	int n;
	for(n = 0; n < CACHEL; n++)
	{
		pages[n] = NULL;
		times[n] = 0;
		names[n] = NULL;
		sizes[n] = 0;
	}
	cachesize = 0;

    listenfd = Open_listenfd(port);
    while (1)
	{
		clientlen = sizeof(clientaddr);
		pt_helper* pt = (pt_helper*)malloc(sizeof(pt_helper));
		pt->connfd = accept(listenfd, (SA *)&clientaddr,(unsigned int *)  
			&clientlen);
		pthread_create(&(pt->thread), NULL, doit, pt);
		pthread_detach(pt->thread);
    }
}

/*
 *  * doit - handle one HTTP request/response transaction
 *
 */
void* doit(void* pt1) 
{
	//signal(SIGPIPE, SIG_IGN);
	pt_helper* pt = (pt_helper*)pt1;
	int fd = pt->connfd;
    int serverFile, amountRead;
    char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE],
		request[MAXLINE];
    char *host, *website, *filename;
	int port;
    rio_t rio, tmprio;
  
	counter++;

	website = uri;
	
    /* Read request line and headers */
    Rio_readinitb(&rio, fd);
    Rio_readlineb(&rio, buf, MAXLINE);
    sscanf(buf, "%s %s %s", method, uri, version);
	//some error checking.
	if(strcasecmp(method, "GET"))
	{
		clienterror(fd, method, "501", "Not Implemented",
			"FoxyProxy does not implement this method");
		Close(pt->connfd);
		free(pt);
		return NULL;
	}
	
	port = parse_website(&website,&filename,&host);
	int n; int found=0;
	for(n = 0; n < CACHEL; n++)
	{
		if(names[n] != NULL){
//		printf("blah blah %s\n",names[n]);
//		printf("foo foo %s\n",website);
		}
		if(names[n] != NULL && !strcmp(website, names[n]))
		{
			found = 1;
			break;
		}
	}
	if(found)
	{
		P(&mut);
		readLock++;
		if(readLock == 1)
		{
			P(&writeLock);
		}
		V(&mut);
	
		times[n] = counter;//time(NULL);
		rio_writen(fd, pages[n], sizes[n]);
		//close(serverFile);
		printf("cached!\n");
		
		P(&mut);
		readLock--;
		if(readLock == 0)
		{
			V(&writeLock);
		}
		V(&mut);
		

		Close(pt->connfd);
		free(pt);
		return NULL;
	}

	serverFile = open_clientfd(host, port);

	//add in request print stuff here
	strcpy(request, method);
	strcat(request, " ");
	strcat(request, filename);
	strcat(request, " HTTP/1.0\r\n");
	strcat(request, HTTPHDR);
	
	rio_writen(serverFile, request, strlen(request));
	//XXX
	//printf(request);
	
	send_requesthdrs(&rio, serverFile, host);

	rio_readinitb(&tmprio, serverFile);
	
	char* buf2 = (char*)malloc(MAX_OBJECT_SIZE);
	int totalRead = 0;
	while ( (amountRead = rio_readnb(&tmprio, buf, MAXBUF)) > 0 )
	{
		rio_writen(fd, buf, amountRead);
		if(totalRead+amountRead <= MAX_OBJECT_SIZE)
		{
			memcpy(buf2+totalRead,buf,amountRead);
		}
		totalRead+=amountRead;
	}

	amountRead = totalRead;
		
	//TODO lock cache
	//FIXME how to delete enough stuff?
	if(amountRead <= MAX_OBJECT_SIZE)
	{
	//	printf("Lock: %d\n", writeLock);
		P(&writeLock);
	//	printf("Lock2: %d\n", writeLock);

		if(amountRead + cachesize <= MAX_CACHE_SIZE)
		{
			time_t lowtime = counter;//time(NULL);
			int leastrecent = -1;
			int n;
			for(n = 0; n < CACHEL; n++)
			{
				if(times[n] < lowtime)
				{
					leastrecent = n;
					lowtime = times[n];
				}
				if(pages[n] == NULL)
				{
					leastrecent = n;
					break;
				}
			}
			n = leastrecent;
			if(pages[n] != NULL)
			{
				free(pages[n]);
				pages[n] = NULL;
				free(names[n]);
				names[n] = NULL;
			}
			void* page = (void*)malloc(amountRead);
			memcpy(page, buf2, amountRead);
			printf("page: \n");
			printf("%s",page);
			char* website_cpy = (char*)malloc(strlen(website)+1);
			strncpy(website_cpy,website,strlen(website));
			website_cpy[strlen(website)]='\0';
			pages[n] = page;
			times[n] = counter;//time(NULL);
			cachesize-=sizes[n];
			sizes[n] = amountRead;
			cachesize+=amountRead;
			names[n] = website_cpy;
		}
		else
		{
			while(cachesize + amountRead > MAX_CACHE_SIZE)
			{
				time_t lowtime = counter;//time(NULL);
				int leastrecent = -1;
				int n;
				for(n = 0; n < CACHEL; n++)
				{
					if(times[n] < lowtime && pages[n] != NULL)
					{
						leastrecent = n;
						lowtime = times[n];
					}
				}
				
				n = leastrecent; 

				free(pages[n]);
				pages[n] = NULL;
				free(names[n]);
				names[n] = NULL;
				cachesize -= sizes[n];
				sizes[n] = 0;
				times[n] = 0;
			}
			int n;
			for(n = 0; n < CACHEL; n++)
			{
				if(pages[n] == NULL) break;
			}
			void* page = (void*)malloc(amountRead);
			memcpy(page, buf2, amountRead);
			pages[n] = page;
			char* website_cpy = (char*)malloc(strlen(website));
			strncpy(website_cpy,website,strlen(website));
			times[n] = counter;//time(NULL);
			names[n] = website_cpy;
			sizes[n] = amountRead;
			cachesize += amountRead;
		}
		V(&writeLock);
	//TODO unlock cache
	}
	free(buf2);
	close(serverFile);
	Close(pt->connfd);
	free(pt);
	return NULL;
}
/* $end doit */

/*
 *  * read_requesthdrs - read and parse HTTP request headers
 *   */
/* $begin read_requesthdrs */
void send_requesthdrs(rio_t *rp, int file, char* host) 
{
	
	char buf[MAXLINE];
	int hosthdr = 0;
	int bufLen;

    bufLen = rio_readlineb(rp, buf, MAXLINE);
	rio_writen(file, buf, bufLen);
	while(strcmp(buf, "\r\n")) 
	{          
		bufLen = rio_readlineb(rp, buf, MAXLINE);
		
		if(!strncasecmp(buf, "Connection", strlen("Connection")))
		{
			continue;
		}
		if(!strncasecmp(buf, "Proxy-Connection", strlen("Proxy-Connection")))
		{
			continue;
		}
		if(!strncasecmp(buf, "User-Agent", strlen("User-Agent")))
		{
			continue;
		}
		if(!strncasecmp(buf, "Accept", strlen("Accept")))
		{
			continue;
		}
		if(!strncasecmp(buf, "Accept-Encoding", strlen("Accept-Encoding")))
		{
			continue;
		}
		if(!strncasecmp(buf, "Host", strlen("Host")))
		{
			hosthdr = 1;
		}
		//this is broken. When we understand how to do Host, we'll fix it.
		

		if(0 && !strncasecmp(buf,"\r\n",strlen("\r\n")) && !hosthdr)
		{
			char tmp[MAXLINE];
			sprintf(tmp, "Host: %s\r\n",host);
			rio_writen(file, tmp, strlen(tmp));
			printf(tmp);
		}
    		rio_writen(file, buf, bufLen);
		if(!strncasecmp(buf,"Cookie",strlen("Cookie")))
		{
			printf("Cookie: <POTENTIALLY BINARY DATA REDACTED>\r\n");
		}
		else
		{
			printf(buf);
		}
	}
}
/* $end read_requesthdrs */

/*
 *  * parse_uri - parse URI into filename and CGI args
 *   *             return 0 if dynamic content, 1 if static
 *    */
/* $begin parse_uri */
int parse_website(char **website, char **filename, char **host) 
{
    //remove http://
	char *fixedUrl = *website + 7;

	int websiteLength;
	int fileLength = 0;
	int portLength = 0;
	int port;
	int hostLength;
	
	
	websiteLength = strlen(fixedUrl);
	
	*filename = strstr(fixedUrl, "/");
	char* portaddr = strstr(fixedUrl,":");
	//if it exists and comes before the beginning of the url
	if(portaddr != NULL && portaddr < *filename)
	{
		portaddr++;//skip the : character
		portLength = (int)(*filename-portaddr);
		char* tempstr = (char*)malloc(sizeof(char)*portLength+1);
		strncpy(tempstr, portaddr, portLength);
		tempstr[portLength]='\0';
		port = atoi(tempstr);
		printf("port switch: %d\n",port);
		free(tempstr);
		tempstr=NULL;
	}
	else
	{
		port = 80;
	}

	if(*filename)
	{
		fileLength = strlen(*filename);
	}
	else
	{
		*filename = "/";
	}
	
	hostLength = websiteLength -  fileLength - portLength;	
	*host = strndup(fixedUrl, hostLength);
	return port;
}

/*
 *  * clienterror - returns an error message to the client
 *   */
/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum, 
		 char *shortmsg, char *longmsg) 
{
    char buf[MAXLINE], body[MAXBUF];

    /* Build the HTTP response body */
    sprintf(body, "<html><title>Tiny Error</title>");
    sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
    sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
    sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
    sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

    /* Print the HTTP response */
    sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    Rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    Rio_writen(fd, buf, strlen(buf));
    Rio_writen(fd, body, strlen(body));
}
/* $end clienterror */

/* Safe version of gethostbyname */
