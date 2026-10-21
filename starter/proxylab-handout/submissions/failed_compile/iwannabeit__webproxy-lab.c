#include <stdio.h>
#include "csapp.h"
/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400


void *thread(void *vargp);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen);  // line:netp:tiny:accept
    Pthread_create(&tid, NULL, thread, connfdp);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
  }
}

void *thread(void *vargp){
  int connfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int clientfd)
{
  int serverfd;
  int is_static; //정적 파일 구분 변수
  struct stat sbuf; // 파일 정보를 담기 위한 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], server_buf[MAXLINE], client_buf[MAXLINE]
      , host[MAXLINE], path[MAXLINE], port[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /*request 라인과 헤더 읽기*/
  //request 라인
  Rio_readinitb(&rio, clientfd);//rio구조체를 초기화한 후 fd 와 rio를 연결
  Rio_readlineb(&rio, buf, MAXLINE); //버퍼에 fd를 저장
  
  //request 헤더
  printf("Request headers:\n");
  printf("%s", buf); //GET/HTTP/1.1
  //sscanf : string scanf 로, 문자를 추출해서 데이터를 변수에 저장
  sscanf(buf, "%s %s %s", method, uri, version);//파싱하여 http메서드, uri, version 정보 추출
  
  //파싱해서 host, port, path 가져오기
  parse_uri(uri, host, port, path);
  printf("----------------------------\n");
  printf("uri: %s host: %s port: %s path: %s\n",uri, host, port, path);
  printf("----------------------------\n");
  //서버에 연결된 클라이언트 소켓의 파일 디스크립터를 serverfd로 저장
  serverfd = Open_clientfd(host, port);
  request(serverfd, host, path); //telnet
  respond(serverfd, clientfd); //server에서 쓰는 serverfd랑 client로 보낼 clientfd 
  Close(serverfd);
}
//요청하는 함수
void request(int targetfd, char *host, char *path){
  char* version = "HTTP/1.0";
  char buf[MAXLINE];

  //HTTP 메서드, 경로, 버전추가
  sprintf(buf, "GET %s %s\n\r", path, version);
  sprintf(buf+ strlen(buf), "Host: %s\r\n", host);
  sprintf(buf + strlen(buf), "%s", user_agent_hdr);

  sprintf(buf + strlen(buf), "Connections: close\r\n", buf);
  sprintf(buf + strlen(buf), "Proxy-Connection: close\r\n\r\n", buf);
  //소켓에 쓰겠다
  Rio_writen(targetfd, buf, (size_t)strlen(buf)); //소켓을 통해 버퍼 내용을 서버로 전송하겠다!
  printf("요청완료");
}

//응답하는 함수
void respond(int targetfd, int clientfd){
  rio_t rio;
  char buf[MAXBUF], reqbuf[MAXBUF];
  int content_length;
  char *cache;

  printf("서버 응답 기다림\n");
  Rio_readinitb(&rio, targetfd); //server받은거 연결

  //서버로부터 header 받아오기
  Rio_readlineb(&rio, buf, MAXLINE);
  printf("첫 줄 %s\n",buf);
  while(strcmp(buf, "\r\n")){ //공백두개인경우까지 반복문 수행
    Rio_readlineb(&rio, buf, MAXLINE);
    printf("%s",buf);
    if(strstr(buf, "Content-Length")){ //content-length가 있는지 확인해서 있으면
      content_length = atoi(strchr(buf, ':') + 1); //content lengt: 다음에 오는 정수를 추출해서 content-lengt 변수에 저장
      printf("%d\n",content_length);
    }
    Rio_writen(clientfd, buf, strlen(buf)); //헤더를 buf사이즈만큼 클라이언트에게 전송    
  }
  //body 받아오기
  // if(content_length > 0){
  //   cache = (char*)malloc(content_length);
  //   Rio_readnb(&rio, cache, content_length);
  //   printf("server received %d byte : Content-Length만큼", content_length);
  //   //클라이언트한테 body전송
  //   Rio_writen(clientfd, cache, content_length);
  // }
  printf("새로 만든 버프:\n %s\n",reqbuf);
  Rio_readnb(&rio, buf, MAXBUF);
  printf("\n그냥버프\n%s\n",buf);
  printf("\n새버프\n%s\n",reqbuf);
  Rio_writen(clientfd, buf, content_length);
  
}
//read_requesthdrs
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")){ //공백두개인경우까지 반복문 수행
    Rio_readlineb(rp, buf, MAXLINE);
  }
  return;
}

//parsing 해주기
int parse_uri(char *uri, char *host, char *port, char *path)
{
  // http://234.234.14:5000/cgb-i/adder
  char *parse_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;

	//www.github.com:80/bckim9489.html
	strcpy(host, parse_ptr);

	strcpy(path, "/"); //path = /

	parse_ptr = strchr(host, '/'); //parse_ptr 포인터
	if(parse_ptr){
		//path = /bckim9489.html
		*parse_ptr = '\0';
		parse_ptr +=1;
		strcat(path, parse_ptr);
	}

	//www.github.com:80
	parse_ptr = strchr(host, ':');
	if(parse_ptr){
		//port = 80
		*parse_ptr = '\0';
		parse_ptr +=1;
		strcpy(port, parse_ptr);
	} else {
		strcpy(port, "80");
	}
}

