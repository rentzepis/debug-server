#include <stdio.h>
#include "csapp.h"
#include "cache.h"


/* Recommended max cache and object sizes */


/* You won't lose style points for including this long line in your code */
static char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
//static char *usr_agent_curl ="curl/7.47.0\r\n";
//static char *accept_curl = "*/*\r\n";
static char *accept_hdr = "Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n";
static char *accept_encoding_hdr = "Accept-Encoding: gzip, deflate\r\n";
static char *connection = "Connection: close\r\n";
static char *prxy = "Proxy-Connection: close\r\n\r\n";
static const char *GET = "GET";
int VERBOSE=0;
int cache_enabled =1;



void onRequest(int fd);
void errorproxy(int fd, char* error, char* msg);
void headers(int fd, char* url, headers_t* head);
int parse_url(char* url, headers_t* headers);

void* thread(void *vargp);
void sigpipe_handler(int sig);



int main(int argc, char **argv)
{
    char* port;
    int listenfd;
    int* connfd;
    struct sockaddr_in addrofclient;
    socklen_t len_addrofclient = sizeof(struct sockaddr_in);
    pthread_t tid;

    Signal(SIGPIPE, sigpipe_handler);
    cache_init();
    /*
        Verificatio to set a Verbose flag (debug) and to see if we have enough arguments
    */
    if(argc == 3 && strcmp(argv[2],"v")){
        VERBOSE = 1;
    }
    if(VERBOSE){
        printf("cmd : %s\n",argv[0]);
        printf("port : %s\n", argv[1]);
    } 

    
    if(argc<2){
        fprintf(stderr, "I need a port to work :(\n");
        exit(0);
    }
    
    port = argv[1];


    listenfd = Open_listenfd(port);
    while(1){   
        connfd = (int *) Malloc(sizeof(int));
        *connfd = Accept(listenfd,(SA *) &addrofclient, &len_addrofclient);
        Pthread_create(&tid, NULL, thread, connfd);
        
    }

    return 0;
}

//Inspired by the echo server of the book
void* thread(void *vargp){
    int connfd = *((int *) vargp);
    if(VERBOSE){
        printf("New thread : fd = %d", connfd);
    }
    Pthread_detach(pthread_self());
    Free(vargp); //Really important to avoid memory leak
    Signal(SIGPIPE, sigpipe_handler);
    onRequest(connfd);
    Close(connfd);
    if(VERBOSE){
        printf("Thread exit : fd = %d", connfd);
    }
    return NULL;

}

/*
    This function handle the REQUESTS (GET). We know from csapp.h that MAXLINE = 8192
*/

void onRequest(int fd){
    char buf[MAXLINE];
    char request_type[MAXLINE];
    char url[MAXLINE]; 
    char http1[MAXLINE];
    char buf_resp[MAXLINE]; 
    rio_t rio_user, rio_resp;
    headers_t* heads;
    heads = (headers_t*) Malloc(sizeof(*heads)*10000000);
    int fd_resp;
    obj_t* the_obj;

    
    /*Initialise fd for request with the rio package*/
    rio_readinitb(&rio_user, fd);
    rio_readlineb(&rio_user,buf,MAXLINE);

    /*If the request is NULL, then we're not handling it*/
    if(!strcmp(buf,"")){
        if(VERBOSE){
            errorproxy(fd,"Not Valid","Buff empty");
        }
        return;
    }

    /*If the request is not NULL but with a bad requestype (or not handled), then we're not handling it*/
    sscanf(buf, "%s %s %s", request_type, url, http1 );

    
    if(VERBOSE){
        printf("Buf : %s\n",buf);
        printf("Request type : %s\nURL : %s\nHTTP : %s\n\n", request_type, url, http1);
        
    }


    if(strcmp(GET, request_type)){
        if(VERBOSE)
            printf("%s is not implemented", request_type);
        return;
    }

    headers(fd,url,heads);

    if(VERBOSE){
        printf("type : %s\n",heads->type);
        printf("host : %s\n", heads->host);
        printf("Port : %s\n", heads->portnumber);
        printf("Path : %s\n",heads->path);
        printf("Connection : %s\n", heads->connection);
        printf("Co proxy : %s", heads->proxy_connection);
        printf("User agent : %s", heads->user_agent);
    }
    /*Cache part*/
    //First we check if someone is already reading, since all the thread can modify isreading, we protect it when it's moified
    if(cache_enabled){
        P(&mutex);
        isreading++;
        if(isreading){
            P(&s); //Here if isreading =1, meaning the thread who reach this line is the first one reading the cache, then we block every other possible access to not have race issues 
        }
        V(&mutex);
        the_obj = find_in_cache(heads);
        if(the_obj != NULL){ //the header is in the cache we have a hit 
            if(VERBOSE)
                printf("Cache hit\n");

            //First we put it at the top of the list
            deletee(the_obj);
            push(the_obj);
            //Then we write the message back to the user screen
            rio_writen(fd,the_obj->tosend, the_obj->size);
            if(VERBOSE)
                printf("Response sent\n");
            
            P(&mutex);
            isreading--;
            if(isreading == 0){
                V(&s);
            }
            V(&mutex);
            printf("Proxy exit, with cache hit\n");
            return;
        }
        P(&mutex);
        isreading--;
        if(isreading == 0){
            V(&s);
        }
        V(&mutex);
        printf("Cache miss\n");
    }

    //If reach there, this means we have a cache miss
    /*Cache part end*/
    fd_resp = Open_clientfd(heads->host, heads->portnumber);
    rio_readinitb(&rio_resp,fd_resp);

    /*Create and send request*/
    sprintf(buf_resp, "GET %s HTTP/1.0\r\n", heads->path);
    rio_writen(fd_resp, buf_resp, strlen(buf_resp));
    sprintf(buf_resp, "Host: %s\r\n", heads->host);
    rio_writen(fd_resp,buf_resp, strlen(buf_resp));
    rio_writen(fd_resp,heads->user_agent, strlen(heads->user_agent));
    rio_writen(fd_resp,accept_hdr, strlen(accept_hdr));
    rio_writen(fd_resp, accept_encoding_hdr, strlen(accept_encoding_hdr));
    rio_writen(fd_resp, heads->connection, strlen(heads->connection));
    rio_writen(fd_resp, heads->proxy_connection, strlen(heads->proxy_connection));

    /*Return the response of the client*/
    size_t n;
    int number;
    char tosend[MAX_OBJECT_SIZE];
    strcpy(tosend, "");
    while ((n = rio_readlineb(&rio_resp, buf_resp, MAXLINE)) != 0) {
        if(VERBOSE){
            printf("Response transmitted %d\n",number);
        }
        number+=n;
        if(number<= MAX_OBJECT_SIZE){
            strcat(tosend, buf_resp);
        }
        rio_writen(fd,buf_resp, n);
        
    }
    /*New cache part*/
    //In case of miss, we have to put the object inside the cache
    if(cache_enabled){
        if(number<= MAX_OBJECT_SIZE){
            the_obj = create_obj(heads, number, tosend);

            //Since multiple thread can access at the same time this part, to avoid a race issue we use P and V functions
            P(&s);

            //if there's not enough space we evict the least recently used
            while(cache_memory_used+number> MAX_CACHE_SIZE){
                evict();
                if(VERBOSE)
                    printf("Eviction");
            }
            //Then we just push the new object
            push(the_obj);
            if(VERBOSE){
                printf("Object cached\n Number of object : %d\n cache memory use : %lu\n ",number_of_obj,cache_memory_used);
            }
            V(&s);

        }
    }


    /*End of cache part*/
    
    /*We free what has been allocated for this thread*/
    //myfree(heads);
    if(VERBOSE){
        printf("Connection closed\n");
        //printf("Component freed\n");
    }

}





// Inspired on https://stackoverflow.com/questions/6285137/html-page-with-a-c-server and https://stackoverflow.com/questions/22077802/simple-c-example-of-doing-an-http-post-and-consuming-the-response
void errorproxy(int fd, char* error, char* msg){
    char buf[MAXLINE], body[MAXBUF];

    /*Response to an error so that it is visible on Internet*/
    sprintf(body, "<html>\n\t<title>Sorry bad request</title>\n");
    sprintf(body, "\t%s%s: %s\n", body, error, msg);
    sprintf(body,"%s </html>\r\n",body);
    sprintf(buf, "HTTP/1.0 %s %s\r\n", error, msg);
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-type: text/html\r\n");
    rio_writen(fd, buf, strlen(buf));
    sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
    rio_writen(fd, buf, strlen(buf));
    rio_writen(fd, body, strlen(body));

}
/*
    Here we retrieve some informations about the header
    Especially we retrieve the hostname, the port and the path to the request
    If there is an error in the specified path, it returns 0
    If there isn't it returns 1
*/
int parse_url(char* url, headers_t* headers){
    const char* tmp;
    const char* tmp2;
    const char* ptr_url;
    int len;

    ptr_url = url;

    tmp = strstr(url, ":");
    if(tmp==NULL){
        return 0;
    }


    /*Parse type*/
    len =  tmp-ptr_url;
    headers->type = (char*) Malloc(sizeof(char)*(len+1));
    strncpy(headers->type,ptr_url,len);
    headers->type[len] = '\0';
    if(strcmp("http",headers->type)!=0){
        if(VERBOSE){
            printf("Error : type %s isn't valid\n",headers->type);
        }
        return 0;
    }

    tmp++;
    ptr_url = tmp;
    /* If there is no // after the http, there's an error*/
    if(ptr_url[0] != '/' && ptr_url[1] !='/'){
        if(VERBOSE){
            printf("Miss of //");
        }
        return 0;
    }
    tmp += 2;
    ptr_url = tmp;

    /*Parse Host, Here we can have a port or no port specified*/
    tmp2 = strstr(ptr_url, ":");
    tmp = strstr(ptr_url, "/");

    if(tmp2 == NULL){
        //No port specified
        headers->portnumber = (char*) Malloc(sizeof(char)*(3));
        headers->portnumber = "80\0";

        len = tmp-ptr_url;
        headers->host = (char*) Malloc(sizeof(char)*(len+1));
        strncpy(headers->host,ptr_url,len);
        headers->host[len] = '\0';
    }
    else{
        if(tmp2<tmp){
            len = tmp2-ptr_url;
            headers->host = (char*) Malloc(sizeof(char)*(len+1));
            strncpy(headers->host,ptr_url,len);
            headers->host[len] = '\0';

            len = tmp-tmp2-1;
            headers->portnumber = (char*) Malloc(sizeof(char)*(len+1));
            strncpy(headers->portnumber,tmp2+1,len);
            headers->portnumber[len] = '\0';
            ptr_url = tmp;
        }
        else{
            len = tmp-ptr_url;
            headers->host = (char*)  Malloc(sizeof(char)*(len+1));
            strncpy(headers->host,ptr_url,len);
            headers->host[len] = '\0';

            headers->portnumber = (char*) Malloc(sizeof(char)*(3));
            headers->portnumber = "80\0";
            ptr_url = tmp;
        }
    }


    /*Retrieving the path*/

    while(strcmp(tmp,"")){
        tmp++;
    }
    len = tmp - ptr_url;
    headers->path = (char*) Malloc(sizeof(char)*(len+1));
    strncpy(headers->path, ptr_url, len);
    headers->path[len] = '\0';




    return 1;

} 

/*
    This function return the header of a request
*/
void headers(int fd,char* url, headers_t* head){
    
    head->connection = connection;
    head->proxy_connection = prxy;


    head->user_agent = user_agent_hdr;
    if(!parse_url(url,head)){
        errorproxy(fd,"URL isn't valid","Check the URL");
    }


}

void sigpipe_handler(int sig) {
    printf("Do nothiiiiiiing\n");
    return;
}
