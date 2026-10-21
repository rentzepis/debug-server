#include <stdio.h>
#include "csapp.h"
#include <malloc.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/*
동시성
*/
/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

void doit(int fd);
int proxy_to_server(int fd, rio_t *rp);
void proxy_to_client();
void * thread(void *vargp);

struct node* createNode(char* key, char* header, char* body);
int hashFunction(char* key);
void add(char* key, char* header, char* body);
void remove_key(char* key);
struct node* search(char* key);
void display();

// 해시 테이블 노드 구조체 선언
struct node {
    char* key; // 해시 함수에 사용될 키
    char* value; // key 가 가지고 있는 데이터
    char* header;
    char* body;
    struct node* next; // 다음 노드를 가르키는 포인터
    struct node* previous; // 이전 노드를 가르키는 포인터
};
// 해시테이블 구조체 선언
struct bucket{
    struct node* head; // 버켓 가장 앞에 있는 노드의 포인터
    int count; // 버켓에 들어있는 노드의 개수
};
// 해시테이블
struct bucket* hashTable = NULL; 
int BUCKET_SIZE = 10; // 버켓의 총 길이





int main(int argc, char **argv) {
  /*
  동시성 X
  */
  /*
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  // command line args
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //SA - socketAddr
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);

    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);
    Close(connfd);
  }
  */

  /*
  동시성
  */
  int listenfd, *connfdp;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  /*
  Cache
  */
  //Cache 용 HashTable 생성
  hashTable = (struct bucket *)Malloc(BUCKET_SIZE * sizeof(struct bucket));

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);



  while (1) {
    clientlen = sizeof(clientaddr);
    connfdp = Malloc(sizeof(int));
    *connfdp = Accept(listenfd, (SA *)&clientaddr, &clientlen); //SA - socketAddr
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfdp);
  }
}


// thread 함수
void *thread(void *vargp)
{
  int connfd = *((int *) vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(connfd);
  Close(connfd);
  return NULL;
}

void doit(int fd)
{
  /*
  웹 트랜잭션 1개 처리
  0. 클라이언트 -> 프록시 (이미 요청받은 상태)
  1. 요청 파싱
  2. 프록시 -> 메인서버
  3. 메인서버 -> 프록시
  4. 프록시 -> 클라이언트
  */
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE],buf2[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers*/
  // 1. 요청 유효성검사 및 파싱
  Rio_readinitb(&rio, fd);

  // 2.프록시 -> 메인서버
  proxy_to_server(fd, &rio);
}



void analyze_request()
{
  /*
  클라이언트 요청 분석
  1. url파싱
  2. 헤더가져오기
  3. 유효성검사(추후구현)
  */
 return;
}


int proxy_to_server(int fd, rio_t *rp){
  /*
  프록시 -> 서버
  1. uri파싱
  1-1. 캐시 유무 확인(있으면 바로 메인서버로 보내기)
  2. 프록시 서버에 클라이언트 소켓 생성
  3. 요청 정보(헤더) 메인서버로 보내기
  4. 메인서버에서 받은 값 클라이언트로 보내기
  */
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char *p, *temp;
  char hostname[MAXLINE], port[MAXLINE], location[MAXLINE], buf[MAXLINE], header_info[MAXLINE];
  char request_info[MAXLINE] = {0};
  char result_header[MAXLINE] = {0};
  char *result_body;
  rio_t rio;

  Rio_readlineb(rp, buf, MAXLINE);
  printf("Request headers :\n");
  sscanf(buf, "%s %s %s", method, uri, version);

  //1. uri 파싱
  p = uri + 7;
  temp = strchr(p, '/');
  strcpy(location, temp);
  *temp = '\0';
  // port
  temp = strchr(p, ':');
  strcpy(port, temp+1);
  *temp = '\0';
  // hostname
  strcpy(hostname, p);

  //1-1. 캐시 유무 확인
  struct node* cache_exists = search(location);
  if (cache_exists!= NULL){
    // connect 맺기
    Rio_writen(fd, cache_exists->header, strlen(cache_exists->header));
    Rio_writen(fd, cache_exists->body, malloc_usable_size(cache_exists->body)+10);
    return;
  }

  // 2. 프록시 서버에 클라이언트 소켓 생성
  int clientfd = Open_clientfd(hostname, port);

  // request 정보 세팅
  strcat(request_info, method);
  strcat(request_info, " ");
  strcat(request_info, location);
  strcat(request_info, " ");
  strcat(request_info, version);

  // 3. 요청 정보(헤더) 메인서버로 보내기
  printf("request_info: %s\n", request_info);
  // 소켓에 첫줄 쓰기 ex) GET / HTTP/1.1
  Rio_writen(clientfd, request_info, strlen(request_info));

  // 버퍼 한번 빼주기
  Rio_readlineb(rp, header_info, MAXLINE);
  if (strcmp(header_info, "\r\n") != 0){
    Rio_writen(clientfd, header_info, strlen(header_info));
    printf("%s\n", header_info);
  }

  // 헤더값 저장 및 쓰기
  while (strcmp(header_info, "\r\n")) {   // strcmp 문자열 비교 함수
    Rio_readlineb(rp, header_info, MAXLINE);
    Rio_writen(clientfd, header_info, strlen(header_info));
    printf("%s\n", header_info);
  }

  // 4. 메인서버에서 받은 값 클라이언트로 보내기
  // 결과 헤더 담기
  p = NULL; // content-length 체크용 p 초기화
  int body_length; // body길이
  Rio_readinitb(&rio, clientfd);
  while(strcmp(buf, "\r\n")){
    Rio_readlineb(&rio, buf, MAXLINE);
    strcat(result_header, buf);

    // Content-length 파싱
    p = strchr(buf, ':');
    if (p != NULL){
      *p = '\0';
      if (strcmp(buf, "Content-length") == 0){
        body_length = atoi(p+1);
        printf("body_length: %d \n", body_length);
      }
    }
  }


  // Content-length 만큼 문자길이 추출
  result_body = (char*) Malloc(body_length);
  Rio_readnb(&rio, result_body, body_length);

  printf("%s", result_header);
  printf("%s", result_body);

  // listenfd 소켓에 데이터 쓰기
  Rio_writen(fd, result_header, strlen(result_header));
  // Rio_writen(fd, "\r\n", 4);
  Rio_writen(fd, result_body, body_length);

  /* 
  캐시 처리
  */
  // url 저장
  char* cache_key = (char *) Malloc(strlen(location));
  strcpy(cache_key, location);
  
  // 응답 헤더 저장 
  char* cache_header = (char *) Malloc(strlen(result_header));
  strcpy(cache_header, result_header);

  // 응답 바디 저장
  char* cache_body = result_body;
  add(cache_key, cache_header, cache_body);

}



/*
캐시용 해시테이블
*/

// 해쉬테이블 삽입될 때 새로 노드를 생성해주는 함수(초기화)
struct node* createNode(char* key, char* header, char* body){
    struct node* newNode;
    // 메모리 할당
    newNode = (struct node *)Malloc(sizeof(struct node));
    // 사용자가 전해준 값을 대입
    newNode->key = key;
    newNode->header = header;
    newNode->body = body;
    newNode->next = NULL; // 생성할 때는 next를 NULL로 초기화
    return newNode;
}

// 해쉬함수 만들기. 여기서는 단순히 key를 버켓 길이로 나눈 나머지로 함수를 만듦.
int hashFunction(char* key){
    char p[1];
    p[0] = *(key+1);
    return (int)p[0] % BUCKET_SIZE;
}

// 새로 키 추가하는 함수
void add(char* key, char* header, char* body){
    // 어느 버켓에 추가할지 인덱스를 계산
    int hashIndex = hashFunction(key);
    // 새로 노드 생성
    struct node* newNode = createNode(key, header, body);
    // 계산한 인덱스의 버켓에 아무 노드도 없을 경우
    if (hashTable[hashIndex].count == 0){
        hashTable[hashIndex].count = 1;
        hashTable[hashIndex].head = newNode; // head를 교체
    }
    // 버켓에 다른 노드가 있을 경우 한칸씩 밀고 내가 헤드가 된다(실제로는 포인터만 바꿔줌)
    // 버켓에 다른 노드가 있을 경우 한칸씩 밀고 내가 헤드가 된다(실제로는 포인터만 바꿔줌)
    else{
        hashTable[hashIndex].head->previous = newNode; // 추가
        newNode->next = hashTable[hashIndex].head;
        hashTable[hashIndex].head = newNode;
        hashTable[hashIndex].count++;
    }
}

// 키를 삭제해주는 함수
void remove_key(char* key){
    int hashIndex = hashFunction(key);
    // 삭제가 되었는지 확인하는 flag 선언
    int flag = 0;
    // 키를 찾아줄 iterator 선언
    struct node* node;
    // struct node* before; // 필요 없음!
    // 버켓의 head부터 시작
    node = hashTable[hashIndex].head;
    // 버켓을 순회하면서 key를 찾는다.
    while (node != NULL) // NULL 이 나올때까지 순회
    {
        if (strcmp(node->key,key)==0){
            // 포인터 바꿔주기 노드가 1 . 헤드인 경우 2 . 헤드가 아닌경우
            if (node == hashTable[hashIndex].head){
                node->next->previous = NULL; // 추가 이제 다음께 헤드가 되었으므로 previous가 없음
                hashTable[hashIndex].head = node->next; // 다음 노드가 이제 헤드
            }
            else{
                node->previous->next = node->next; // 내 전 노드의 앞이 이제 내 앞 노드
                node->next->previous = node->previous; // 내 앞 노드의 전이 이제 내 전 노드
            }
            // 나머지 작업 수행
            hashTable[hashIndex].count--;
            free(node->key);
            free(node->value);
            free(node->header);
            free(node->body);
            free(node);
            flag = 1;
        }
        node = node->next;
    }
    // flag의 값에 따라 출력 다르게 해줌
    if (flag == 1){ // 삭제가 되었다면
        printf("\n [ %s ] 이/가 삭제되었습니다. \n", key);
    }
    else{ // 키가 없어서 삭제가 안된 경우
        printf("\n [ %s ] 이/가 존재하지 않아 삭제하지 못했습니다.\n", key);
    }
}

// 키를 주면 value를 알려주는 함수
struct node* search(char* key){
    int hashIndex = hashFunction(key);
    struct node* node = hashTable[hashIndex].head;
    int flag = 0;
    while (node != NULL)
    {
        if (strcmp(node->key,key)==0){
            flag = 1;
            break;
        }
        node = node->next;
    }
    if (flag == 1){ // 키를 찾았다면
        printf("\n 키는 [ %s ] 헤더는 [ %s ], 바디는 [ %s ] 입니다. \n", node->key,  node->header, node->body);
        return node;
    }
    else{
        printf("\n 존재하지 않은 키는 찾을 수 없습니다. \n");
        return NULL;
    }
    
}

// 해시테이블 전체를 출력해주는 함수
void display(){
    // 반복자 선언
    struct node* iterator;
    printf("\n========= display start ========= \n");
    for (int i = 0; i<BUCKET_SIZE; i++){
        iterator = hashTable[i].head;
        printf("Bucket[%d] : ", i);
        while (iterator != NULL)
        {
            printf("(key : %s, val : %s)  -> ", iterator->key, iterator->value);
            iterator = iterator->next;
        }
        printf("\n");
    }
    printf("\n========= display complete ========= \n");
}




