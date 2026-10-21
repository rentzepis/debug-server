#include "csapp.h"
#include "sbuf.h"
#include "cache.h"

#define NTHREADS 8
#define SBUFSIZE 16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";

void *thread(void *vargp);
void proxy(int fd);
void parse_url(char *url, char *hostname, char *uri, char *ip, char *port);
void make_request_header(int fd, rio_t *rp, char *hostname, char *uri, char *headers);
void read_response_headers(rio_t *rp, char *response_headers, char *response_content_length);

// 메인 쓰레드가 연결 후 connfd를 넣고, 쓰레드들이 connfd를 가져가는 공유 버퍼
sbuf_t sbuf;

// 캐시된 데이터가 저장되는 이중 연결 리스트
cache_header *ch;

int main(int argc, char **argv)
{
  int i, listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid;

  // port를 입력하지 않았으면
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  // 캐시 리스트 초기화
  ch = cache_init();

  // 입력 받은 port에 듣기 식별자 열기
  listenfd = Open_listenfd(argv[1]);

  // 공유 버퍼 초기화 및 쓰레드 생성
  sbuf_init(&sbuf, SBUFSIZE);
  for (i = 0; i < NTHREADS; i++)
  {
    Pthread_create(&tid, NULL, thread, NULL);
  }

  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    printf("=====Accepted connection from (%s, %s)=====\r\n", hostname, port);

    // 연결된 connfd를 공유 버퍼에 넣고 다시 연결 대기
    sbuf_insert(&sbuf, connfd);
  }
}

// 쓰레드 루틴
void *thread(void *vargp)
{
  // 메모리 누수를 막기 위한 detach
  Pthread_detach(pthread_self());

  // 일이 끝나면 다시 일 시작 -> 무한 루프
  while (1)
  {
    // 공유 버퍼에 연결된 connfd가 있으면 가져오기, 없으면 대기 -> 세마포어
    int connfd = sbuf_remove(&sbuf);

    printf("=====Start Work : THREAD %d=====\r\n\r\n", pthread_self());

    // 일하기
    proxy(connfd);

    // 일 끝난 후에 connfd 닫기
    Close(connfd);
    printf("=====Disconnect to Client=====\r\n\r\n");
  }
}

void proxy(int fd)
{
  cache_t *cache;

  int clientfd, filesize;
  char buf[MAXLINE], method[MAXLINE], url[MAXLINE], origin_url[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], ip[MAXLINE], port[MAXLINE], uri[MAXLINE], headers[MAXLINE];
  char response_headers[MAXLINE], response_content_length[MAXLINE];
  char *srcp;
  rio_t rio_p_c, rio_s_p;

  // request 첫번째 요청 파싱 : method url version
  Rio_readinitb(&rio_p_c, fd);
  Rio_readlineb(&rio_p_c, buf, MAXLINE);
  printf("=====Recieve request from Client=====\r\n\r\n");
  sscanf(buf, "%s %s %s", method, url, version);
  strcpy(origin_url, url); // 캐싱을 위한 url 원본 저장

  // request의 url이 같은 캐싱된 데이터 검색
  cache = cache_search(ch, origin_url);

  // 같은 url의 캐싱된 데이터가 있으면 캐시 리스트에서 response하고 return
  if (cache != NULL)
  {
    printf("=====Proxy have Cached Data=====\r\n");
    Rio_writen(fd, cache->response_header, strlen(cache->response_header));
    Rio_writen(fd, cache->response_body, cache->filesize);
    printf("=====Send Response=====\r\n\r\n");
    printf("***Saved Cache:\r\n");
    cache_all_display(ch); // 캐시 리스트 프린트 함수

    return;
  }
  else
    printf("=====Proxy don't have Cached Data=====\r\n\r\n");

  printf("***Request header from Client:\r\n");
  printf("%s", buf);

  // url을 파싱해서 end server에 보낼 request 만들기
  parse_url(url, hostname, uri, ip, port);
  make_request_header(fd, &rio_p_c, hostname, uri, headers);

  printf("***Requset header to Server:\r\n");
  printf("%s", headers);

  // end server와 연결
  clientfd = Open_clientfd(ip, port);
  Rio_readinitb(&rio_s_p, clientfd);
  printf("=====Connect to Server=====\r\n\r\n");

  // request 보내기
  Rio_writen(clientfd, headers, strlen(headers));

  // response header 읽기
  read_response_headers(&rio_s_p, response_headers, response_content_length);
  printf("=====Read Response Header=====\r\n\r\n");
  printf("***Response Header from Server to Client:\r\n");
  printf("%s\r\n\r\n", response_headers);

  // response body 읽기
  // 크기를 response header에 있는 Content-length에서 가져옴
  // response header에 Content-length가 없는 경우를 생각 안함 -> 수정해야함
  filesize = atoi(response_content_length);
  srcp = (char *)Malloc(filesize);
  Rio_readnb(&rio_s_p, srcp, filesize);
  printf("=====Read Response Body=====\r\n\r\n");

  // 캐시 리스트에 해당 url를 가진 데이터가 없으면 캐싱
  if ((cache = cache_search(ch, origin_url)) == NULL)
  {
    printf("=====Try to Save Response to Cache=====\r\n");
    cache_append(ch, origin_url, response_headers, srcp, filesize);
    printf("***Saved Cache:\r\n");
    cache_all_display(ch);
  }

  // response header와 body를 다시 client에게 전달
  Rio_writen(fd, response_headers, strlen(response_headers));
  printf("=====Send Response Header to Client=====\r\n\r\n");
  Rio_writen(fd, srcp, filesize);
  printf("=====Send Response Body to Client=====\r\n\r\n");

  // body를 받았던 공간 할당 해제
  Free(srcp);

  // end server와 연결 해제
  Close(clientfd);
  printf("=====Disconnect to Server=====\r\n\r\n");
}

void parse_url(char *url, char *hostname, char *uri, char *ip, char *port)
{
  char *ptr, *start;
  char webserver_port[] = "80"; // 포트가 안적혀 있으면 기본 포트

  // "http://"가 있을 때와 없을 때 처리
  start = url;
  if (strstr(url, "http://"))
    start = url + 7; // skip 'http://'

  // host와 uri 분리
  ptr = index(start, '/');
  if (ptr)
  {
    strncpy(hostname, start, (size_t)(ptr - start));
    strcpy(uri, ptr);
  }

  // host에서 ip와 port 분리
  ptr = index(hostname, ':');
  if (ptr)
  {
    strncpy(ip, hostname, (size_t)(ptr - hostname));
    strcpy(port, ptr + 1);
  }
  else
  {
    strcpy(ip, hostname);
    strcpy(port, webserver_port);
  }
}

void make_request_header(int fd, rio_t *rp, char *hostname, char *uri, char *headers)
{
  char *header_data;
  char header_buf[MAXLINE], buf[MAXLINE], header_name[MAXLINE];

  // 필수 request header 입력
  sprintf(header_buf, "GET %s HTTP/1.0\r\n", uri);
  sprintf(header_buf, "%sHost: %s\r\n", header_buf, hostname);
  sprintf(header_buf, "%s%s\r\n", header_buf, user_agent_hdr);
  sprintf(header_buf, "%sConnection: close\r\n", header_buf);
  sprintf(header_buf, "%sProxy-Connection: close\r\n", header_buf);

  while (strcmp(buf, "\r\n"))
  {
    // request header를 한줄씩 읽어옴
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);

    // header를 name과 data로 분리
    header_data = index(buf, ':');
    if (header_data)
    {
      strncpy(header_name, buf, (size_t)(header_data - buf));
      header_name[(size_t)(header_data - buf)] = '\0';
      header_data += 2;

      // header name이 필수 request header name이면 이미 입력했으므로 skip
      if (!strcasecmp(header_name, "Host") || !strcasecmp(header_name, "User-Agent") || !strcasecmp(header_name, "Connection") || !strcasecmp(header_name, "Proxy-Connection"))
        continue;
      // 아니면 그대로 다시 받아 적음
      else
        sprintf(header_buf, "%s%s: %s\r\n", header_buf, header_name, header_data);
    }
  }

  sprintf(header_buf, "%s\r\n", header_buf);
  strcpy(headers, header_buf);
  return;
}

void read_response_headers(rio_t *rp, char *response_headers, char *response_content_length)
{
  char *header_data;
  char buf[MAXLINE], header_name[MAXLINE], headers[MAXLINE];

  // buf 초기화 -> 첫번째 response header 읽기
  Rio_readlineb(rp, buf, MAXLINE);
  sprintf(headers, "%s", buf);

  while (strcmp(buf, "\r\n"))
  {
    // response header 한줄씩 읽기
    Rio_readlineb(rp, buf, MAXLINE);

    // response header를 name과 data로 분리
    header_data = index(buf, ':');
    if (header_data)
    {
      strncpy(header_name, buf, (size_t)(header_data - buf));
      header_name[(size_t)(header_data - buf)] = '\0';
      header_data += 2;

      // response header에서 content-length data 추출
      if (!strcasecmp(header_name, "Content-length"))
      {
        strcpy(response_content_length, header_data);
      }
    }

    sprintf(headers, "%s%s", headers, buf);
  }

  strcpy(response_headers, headers);

  return;
}
