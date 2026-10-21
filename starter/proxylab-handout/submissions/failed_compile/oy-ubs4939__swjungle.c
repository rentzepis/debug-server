#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include "csapp.h"
#include "sbuf.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

char* indexList[26];
unsigned int totalSize;
char* LRUP_first;
char* LRUP_end;

typedef struct LRU_list {
	struct LRU_list * prev; 
  char * key;
  unsigned int size;
	struct LRU_list * next;
}LRU_list;

typedef struct DLinked_list {
	struct DLinked_list * prev; 
  char * key;
  char * header;
  char * body;
  unsigned int size;
	struct DLinked_list * next;
}DLinked_list;



// Double linked list
struct DLinked_list* DL_init(char* key,unsigned int size,char *header,char *body);
void DL_insert(int index, DLinked_list* newNode);
void DL_delete(DLinked_list* delete_node,char* indexList);

struct LRU_list* LRU_list_init(char* key,int size);
void LRU_list_insert(LRU_list* newNode);
void LRU_list_delete(LRU_list* delete_node);

// hash
int charToHashInt(char* firstCahr);
void* thread(void *vargp);
void do_it(int fd, int clientfd,char* method,char* path,char* version);
void get_connection(int fd);
void read_requesthdrs(rio_t *rp);
void read_body(rio_t *rp,char* send);
void *thread(void *vargp);
sbuf_t sbuf;

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {

  int listenfd, connfd;
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

  sbuf_init(&sbuf,SBUFSIZE);
  for (int i = 0; i < NTHREADS; i++)
  {
    Pthread_create(&tid,NULL,thread,NULL);
  }
  
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,&clientlen);  // line:netp:tiny:accept
    sbuf_insert(&sbuf,connfd);
  }
}


void get_connection(int coonfd){
  int clientfd;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE], header[MAXLINE], content[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE],temp[MAXLINE], hostname[MAXLINE],port[MAXLINE],path[MAXLINE],send[MAXLINE];
  rio_t rio;
  char * p;
  unsigned int contentlength;
  /* Rad request line and headers */
  Rio_readinitb(&rio, coonfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s %s ", method, uri, version);
  // http:// 잘래내서 temp에 임시 저정(localhost:8000/home.html)
  strcpy(temp, uri+7);
  p = strchr(temp, '/');
  // localhost:8000/home.html (/)기준으로 home.html 저장
  strcpy(path,p+1);
  *p = '\0';
  p = strchr(temp,':');
  *p = '\0';
  //(:) 기준으로 localhost 와 8000 나눔
  strcpy(hostname,temp);
  strcpy(port,p+1);

  // 해더 읽기(무시)
  read_requesthdrs(&rio);

  // 캐쉬 탐색
  int hashIndex = charToHashInt((char*)path);
  DLinked_list* node = (DLinked_list*) indexList[hashIndex];

   while (node!= NULL)
    {
      node = node->next;
    }
  node = (DLinked_list*) indexList[hashIndex];
  // 노드가 널이 아니면
  if(node != NULL){
    // while문 돌면서 탐색
    while (node!= NULL)
    {
      // 돌면서 key(경로)가 같은지 확인
      if (!strcmp(node->key,path))
      {
        // 경로가 같은 값이 있으면 안에서 헤더와 바디를 가져와서 바로 써줌.
        Rio_writen(coonfd,node->header,strlen(node->header));
        Rio_writen(coonfd,node->body,(node->size) - strlen(node->header));
        // 따로 tiny 와 연결을 안해줬기 떄문에 바로 리턴

        LRU_list * moveNode = (LRU_list *)LRUP_first;
        while (moveNode!=NULL)
        {
          // 무한 루프
          // LRU 맨앞으로 꺼내오기.
          if(!strcmp(moveNode->key,path)){
            LRU_list_move_first(moveNode);
            break;
          }

          moveNode = moveNode->next;
        }
        return;
      }
      // 첫번째 노드가 다르다면, 그다음 두번째 노드로 탐색 시작
      // sprintf(stderr,"%s",(char*)node->key);
      node = node->next;
    }
  }
  // while문을 빠져나올때까지 캐쉬에 없으므로 tiny와 연결
  clientfd = Open_clientfd(hostname,port);

  // proxy -> tiny로 요청 헤더 작성
  sprintf(buf,"%s /%s HTTP/1.0\r\n",method,path);
  sprintf(buf, "%sHost: %s\r\n",buf,hostname);
  sprintf(buf, "%s%s",buf,user_agent_hdr);
  sprintf(buf, "%sConnection: close\r\n",buf);
  sprintf(buf, "%sProxy-Connection: close\r\n\r\n",buf);
  // 요청 헤더 전송
  Rio_writen(clientfd,buf,strlen(buf));

  // 응답 첫줄 읽어오기
  Rio_readinitb(&rio, clientfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  Rio_writen(coonfd,buf,strlen(buf));
  // 캐쉬에 저장하기 위에 header에 저장
  sprintf(header,buf);



  // 개행문자 만나기 전까지 계속해서 반복적으로 한줄씩 읽으면서 client에 전송하고 header에 담아두기
  // header에 따로 담는 이유는 캐쉬에 저장해야하기 때문.
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(&rio, buf, MAXLINE);
    sprintf(header,"%s%s",header,buf);
    Rio_writen(coonfd,buf,strlen(buf));
    // 두번쨰 바디를 읽어 올때 Content-length 만큼 읽어야하기 때문에 따로 변수에 지정해둠.
    if (strstr(buf,"Content-length:"))
    {
      char* temp;
      p = index(buf,":");
      temp = (char*)buf+15;
      contentlength = atoi(temp);
    }
  }

  char *mm_header,*mm_body,*mm_path;
  if(contentlength){
    // contentlength가 0이 아니라면 (데이터가 있다면) malloc에 Content-length만큼 할당해주기.
    mm_body =(char*) malloc(contentlength);
    // Content-length 만큼 나머지 읽어와서 저장하기.
    Rio_readnb(&rio,mm_body,contentlength);
    // client 측으로 바로 쏴주기.
    Rio_writen(coonfd,mm_body,contentlength);
  }

  // content가 최대 크기보다 작다면 캐쉬로 저장하기.
  if (contentlength <= MAX_OBJECT_SIZE)
  {
    //전체 크기가 캐쉬 허용범위를 넘어가면 삭제하기
    while ((totalSize + (unsigned int)strlen(header) + contentlength) >(unsigned int) MAX_CACHE_SIZE)
    {
      //while문 돌면서 값이 작아질때까지 계속 삭제
      // 마지막 노드 가져오기
      LRU_list* delete_LRU_Node = (LRU_list*)LRUP_end;
      char path2 = delete_LRU_Node->key;
      int tempindex = charToHashInt(path2);
      DLinked_list* cur = (DLinked_list*) indexList[tempindex];

      // 삭제 노드 검색
      while (cur!= NULL)
      {
        // 돌면서 key(경로)가 같은지 확인
        if (!strcmp(cur->key,path2))
        {
          // 같은 함수
          DL_delete(cur,indexList[tempindex]);
          LRU_list_delete(delete_LRU_Node);
          break;
        }
        // 첫번째 노드가 다르다면, 그다음 두번째 노드로 탐색 시작
        cur = cur->next;
        // LRU 먼저 삭제하고 그 키값 가져와서 hash 테이블도 나머지 삭제.
      }
    }
  
    // 해더만큼 할당해주기.
    mm_header =(char*) malloc(strlen(header));
    mm_path =(char*) malloc(strlen(path));
    // 바디는 따로 할당안해주고 위쪽에서 사용. 만약에 크기가 작다면 따로 프리 해줌.
    mm_header = header;
    mm_path = path;
    
    // 새로 연결 리스트 생성해서 이닛으로 안에 정보 채워넣고 맨앞에 삽입
    struct DLinked_list* newNode =  DL_init(mm_path,strlen(header) + contentlength,mm_header,mm_body);
    DL_insert(hashIndex,newNode);
    totalSize += strlen(header) + contentlength;

    DLinked_list* node = (DLinked_list*) indexList[hashIndex];
    while (node!= NULL)
    {
      node = node->next;
    }

    //LRU 를 위한 연결 리스트 노드 생성
    LRU_list* new_LRU_list = LRU_list_init(path,strlen(header) + contentlength);
    LRU_list_insert(new_LRU_list);
    Close(clientfd);
    return;
  }
  free(mm_body);
  Close(clientfd);
  return;
}

void read_requesthdrs(rio_t *rp){
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    // printf("%s",buf);
  }
  return;
}

// 스레드 관한 함수. 다시 보고 주석 적기
void *thread(void *vargp){
 pthread_detach(pthread_self());
 while (1)
 {
  int connfd = sbuf_remove(&sbuf);
  get_connection(connfd);
  Close(connfd);
 }
}

// 해쉬 함수. 간단하게 첫글자만 떼와서 인덱스 추출.
int charToHashInt(char*path){

    char firstChar = path[0];
    if (firstChar >= 65 && firstChar <= 90){
      firstChar += 32;
    } 
    return (int) (firstChar%26);
}

//헤더와 바디, 사이즈를 받아와서 path를 키로 노드 객체 생성
struct DLinked_list* DL_init(char* key,unsigned int size,char *header,char *body){
  DLinked_list *newNode;
  newNode = (DLinked_list *)malloc(sizeof(DLinked_list));
  newNode->key = key;
  newNode->prev = NULL;
  newNode->next = NULL;
  newNode->size = size;
  newNode->header = header;
  newNode->body = body;
  return newNode;
}

// 삽입은 무조건 첫번째로 해줌. 처리가 상수시간.
void DL_insert(int index, DLinked_list* newNode){
  DLinked_list* firstNode =(DLinked_list *) indexList[index];
  if (firstNode == NULL)
  {
    indexList[index] = newNode;
  }
  else{
    newNode->next = firstNode;
    firstNode->prev = newNode;
    indexList[index] = newNode;
    
    firstNode =(DLinked_list *) indexList[index];

  }
}

//삭제
void DL_delete(DLinked_list* delete_node,char* indexList){
  struct DLinked_list * prev_node = delete_node->prev;
  struct DLinked_list * next_node = delete_node->next;
  // prev가 Null 일때.
  if (prev_node == NULL){
    // 처음일때.
    if (next_node == NULL){
      // next도 null 둘다 없음
      indexList = NULL;
    }else{
      // 첫번째 노드 삭제
      next_node->prev = NULL;
      indexList = next_node;
    }
  }else{
    // prev가 존재.
    // next가 없음
    if (next_node == NULL)
    {
      prev_node->next = NULL;
    // next가 있음
    }else{
      prev_node->next = next_node;
      next_node->prev = prev_node;
    }
  }
  // 전부 할당되어있던 것들 해제.
  free(delete_node->key);
  free(delete_node->header);
  free(delete_node->body);
  free(delete_node);
}

// LRU_list객체 새로 생성
struct LRU_list* LRU_list_init(char* key,int size){
  LRU_list * newNode;
  newNode = (LRU_list *)malloc(sizeof(LRU_list));
  newNode->key = key;
  newNode->size = size;
  newNode->prev = NULL;
  newNode->next = NULL;
  return newNode;
}

// 저장은 맨앞에 상수시간.
void LRU_list_insert(LRU_list* newNode){
  LRU_list* firstNode =(LRU_list *) LRUP_first;
  if (firstNode == NULL)
  {
    LRUP_first = newNode;
    LRUP_end = newNode;
  }
  else{
    newNode->next = firstNode;
    firstNode->prev = newNode;
    LRUP_first = newNode;
  }
}

void LRU_list_move_first(LRU_list* move_node){
  LRU_list* firstNode =(LRU_list *) LRUP_first;
  struct LRU_list * prev_node = move_node->prev;
  struct LRU_list * next_node = move_node->next;

  // prev가 Null이 아닐때 (첫번째가 아님)
  if (prev_node != NULL)
 {
    // prev가 존재.
    // next가 없음
    if (next_node == NULL)
    {
      prev_node->next = NULL;
      LRUP_end = prev_node;
    // next가 있음
    }else{
      prev_node->next = next_node;
      next_node->prev = prev_node;
    }
      move_node->next = firstNode;
      firstNode->prev = move_node;
      LRUP_first = move_node;
  }
  move_node->prev = NULL;
}

void LRU_list_delete(LRU_list* delete_node){
  struct LRU_list * prev_node = delete_node->prev;
  struct LRU_list * next_node = delete_node->next;

  // prev가 Null 일때.
  if (prev_node == NULL){
    // 처음일때.
    if (next_node == NULL){
      // next도 null 둘다 없음
      LRUP_first = NULL;
      LRUP_end = NULL;
    }else{
      // 첫번째 노드 삭제
      next_node->prev = NULL;
      LRUP_first = next_node;
    }
  }else{
    // prev가 존재.
    // next가 없음
    if (next_node == NULL)
    {
      prev_node->next = NULL;
      LRUP_end = prev_node;
    // next가 있음
    }else{
      prev_node->next = next_node;
      next_node->prev = prev_node;
    }
  }
  // 전부 할당되어있던 것들 해제.
  free(delete_node->key);
  free(delete_node);
}
