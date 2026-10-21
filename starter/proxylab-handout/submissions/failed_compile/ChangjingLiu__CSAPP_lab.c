#include <stdio.h>
#include "csapp.h"


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void *thread(void *vargp);
void doit(int fd);\
int parse_uri(char *uri,char *hostname,char *path,char *port);
void read_requesthdrs(rio_t *rp);
void build_http_header(rio_t *rio,char *uri,char *hostname,char *path,char *endserver_http_header);
void build_http_header1(char *http_header,char *hostname,char *path,int port,rio_t *client_rio);
//int connect_endserver(char *hostname, int port, char *path );
int connect_endServer(char *hostname,int port,char *http_header);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    pthread_t tid;
    char hostname[MAXLINE], port[MAXLINE];
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;

    if(argc !=2){
        fprintf(stderr,"usage: %s <port>\n",argv[0]);
        exit(1);
    }
    
    listenfd = Open_listenfd(argv[1]);
    //多线程参考p695，图12-14
    while(1){
        clientlen = sizeof(clientaddr);
        connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
        Getnameinfo((SA*)&clientaddr,clientlen,hostname,MAXLINE,port,MAXLINE,0);
        printf("Accepted connection from (%s %s)\n",hostname,port);
        //doit(connfd);
        //Close(connfd);
        Pthread_create(&tid, NULL, thread, (void *)connfd);
    }

    printf("%s", user_agent_hdr);
    return 0;
}

void *thread(void *vargp) {
    int connfd = (int)vargp;
    Pthread_detach(pthread_self());//分离线程
    //Free(vargp);
    doit(connfd);
    Close(connfd);
}

void doit(int connfd){
    int end_serverfd; //服务器描述符
    char buf[MAXLINE],method[MAXLINE],uri[MAXLINE],version[MAXLINE];
    char endserver_http_header[MAXLINE];
    rio_t proxy_rio, server_rio;
    char hostname[MAXLINE],path[MAXLINE];
    char port[MAXLINE];

    //读取客户端请求行
    Rio_readinitb(&proxy_rio, connfd);
    Rio_readlineb(&proxy_rio, buf, MAXLINE);
    printf("Request headers:\n");
    printf("%s", buf);
    sscanf(buf, "%s %s %s", method, uri, version);
    //printf("123%s/n",uri);
    if(strcasecmp(method,"GET")){
        printf("Proxy does not implement the method");
        return;
    }
    //read_requesthdrs(&proxy_rio); /////不用加这一行
    
    parse_uri(uri,hostname,path,port);
    //printf("hostname:%s\r\n",hostname);
    //printf("path:%s\n",path);
    //printf("port:%d\r\n",atoi(port));
    build_http_header(&proxy_rio,uri,hostname,path,endserver_http_header);
    //build_http_header1(endserver_http_header,hostname,path,port,&proxy_rio);
    //printf("new_header:%s\r\n",endserver_http_header);
    /*connect to the end server*/
    end_serverfd = Open_clientfd(hostname,port);
    if(end_serverfd<0){
        printf("connection failed\n");
        return;
    }
    Rio_readinitb(&server_rio,end_serverfd);
    /*write the http header to endserver*/
    Rio_writen(end_serverfd,endserver_http_header,strlen(endserver_http_header));

    /*receive message from end server and send to the client*/
    size_t n;
    while((n=Rio_readlineb(&server_rio,buf,MAXLINE))!=0)
    {
        printf("proxy received %d bytes,then send\n",n);
        Rio_writen(connfd,buf,n);
    }
    Close(end_serverfd);
}


int parse_uri(char *uri,char *hostname,char *path,char *port){
    if (strstr(uri, "http://") != uri) {
        fprintf(stderr, "Error: invalid uri!\n");
        exit(0);
    }
    uri += strlen("http://");
    char *c = strstr(uri, ":");
    *c = '\0';
    strcpy(hostname, uri);
    uri = c + 1;
    //printf("aaa  %s\r\n",uri);
    c = strstr(uri, "/");
    *c = '\0';
    strcpy(port, uri);
    //printf("port:  %s\r\n",port);
    *c = '/';
    strcpy(path, c);
    //printf("path:  %s\r\n",path);
    
    return 0;
}

void read_requesthdrs(rio_t *rp) 
{
    char buf[MAXLINE];

    Rio_readlineb(rp, buf, MAXLINE);
    while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
	    Rio_readlineb(rp, buf, MAXLINE);
	    printf("%s", buf);
    }
    return;
}

void build_http_header(rio_t *rio,char *uri,char *hostname,char *path,char *endserver_http_header){
    //printf("begin\r\n");
    
    static const char* Con_hdr = "Connection: close\r\n";
    static const char* Pcon_hdr = "Proxy-Connection: close\r\n";
    char buf[MAXLINE];
    char Reqline[MAXLINE], Host_hdr[MAXLINE], Cdata[MAXLINE];//分别为请求行，Host首部字段，和其他不东的数据信息 
    sprintf(Reqline, "GET %s HTTP/1.0\r\n", path);   //获取请求行
    while (Rio_readlineb(rio, buf, MAXLINE) > 0){
        
        //读到空行就算结束，GET请求没有实体体
        if (strcmp(buf, "\r\n") == 0){
            strcat(Cdata, "\r\n");
            break;          
        }
        else if (strncasecmp(buf, "Host:", 5) == 0){
            strcpy(Host_hdr, buf);
        }
        
        else if (!strncasecmp(buf, "Connection:", 11) && !strncasecmp(buf, "Proxy_Connection:", 17) &&!strncasecmp(buf, "User-agent:", 11)){
            strcat(Cdata, buf);
        }
    }
    if (!strlen(Host_hdr)){
        //如果Host_hdr为空，说明该host被加载进请求行的URL中，我们格式读从URL中解析的host
        sprintf(Host_hdr, "Host: %s\r\n", hostname); 
    }
    
    sprintf(endserver_http_header, "%s%s%s%s%s%s", Reqline, Host_hdr, Con_hdr, Pcon_hdr, user_agent_hdr, Cdata);
    printf("new_header:%s\r\n",endserver_http_header);

    return;
}

int connect_endserver(char *hostname, int port, char *path )
{
    static const char *user_agent = "User-Agent: Mozilla (X11; Linux i386; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
    static const char *accept_str= "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\nAccept-Encoding: gzip, deflate\r\n";
    static const char *connection = "Connection: close\r\nProxy-Connection: close\r\n";
    
    char buf[MAXLINE];
    /* connect to server */
    int proxy_clientfd;
    proxy_clientfd=open_clientfd(hostname,port);
 
    /* if failed return */
    if(proxy_clientfd<0)
        return proxy_clientfd;
 
    /* write request to server */
    sprintf(buf,"GET %s HTTP/1.0\r\n", path);
    Rio_writen(proxy_clientfd,buf,strlen(buf));
    sprintf(buf,"Host: %s\r\n",hostname);
    Rio_writen(proxy_clientfd,buf,strlen(buf));
    Rio_writen(proxy_clientfd,user_agent,strlen(user_agent));
    Rio_writen(proxy_clientfd,accept_str,strlen(accept_str));
    Rio_writen(proxy_clientfd,connection,strlen(connection));
    Rio_writen(proxy_clientfd,"\r\n",strlen("\r\n"));
    printf("request to server is done.");
    return proxy_clientfd;
}

/*Connect to the end server*/
inline int connect_endServer(char *hostname,int port,char *http_header){
    char portStr[100];
    sprintf(portStr,"%d",port);
    return Open_clientfd(hostname,portStr);
}


