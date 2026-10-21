#include <stdio.h>
#include "csapp.h" // 컴씨에서 제공하는 유틸리티 함수들이 정의된 헤더 파일, 소켓 프로그래밍과 관련된 여러 함수들이 포함되어 있음.

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// HTTP 헤더에서 user-agent 정의한 것, 웹 브라우저나 HTTP 클라이언트의 정보를 나타냄.
// 서버가 이 정보를 기반으로 클라이언트의 특성에 맞춰 응답할 수 있도록 함.
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "  // c언어에서는 문자열 리터럴을 여러 줄에 걸쳐 나열할 수 있음. (하나로 처리)
    "Firefox/10.0.3\r\n"; 

void doit(int clientfd);   // 클라이언트의 요청을 처리하는 핵심 함수.
void read_requesthdrs(rio_t *rp, int fd, char *host_name);   // 클라이언트로부터 받은 HTTP 요청 헤더를 읽는다.
void parse_uri(char *uri, char *filename, char *port, char *cgiargs);  // 클라이언트 요청 URI를 분석.
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);   // 클라이언트의 요청이 잘못 됐을 때 오류 메세지를 보냄.


/* 프록시 서버는 클라이언트로부터 연결 요청을 받고, 요청을 처리한 후 응답을 보내는 기본적인 수행을 한다.
 * 이 프로그램은 포트 번호를 *명령줄 인수*로 받는다. */
int main(int argc, char **argv)  //argc는 인수의 개수, argv는 인수의 배열 
{
  signal(SIGPIPE,SIG_IGN);  // 파이프 또는 소켓 통신에서 발생할 수 있는 SIGPIPE 시그널을 무시하는 코드.
  printf("%s", user_agent_hdr);
  
  // 서버가 클라이언트의 연결 요청을 기다리는 리슨 소켓의 파일 디스크립터, 클라이언트가 서버에 연결된 후, 데이터를 주고받기 위한 연결 소켓의 파일 디스크립터
  int listenfd, clientfd; 

  char hostname[MAXLINE], port[MAXLINE];  // 클라이언트의 호스트 이름과 포트 번호를 저장하는 문자열.
  socklen_t clientlen;  // 클라이언트 주소 구조체의 크기. 
  struct sockaddr_storage clientaddr;  // 클라이언트의 주소 정보를 저장하는 구조체.



 // 명령줄 인수로 포트 번호가 제대로 입력됐는지 확인. 정확히 하나(포트번호)의 명령줄 인수가 전달 되어야 한다.
  if (argc != 2) {  // 전달 된 인수중 첫번째 인수는 무조건 프로그램 이름이니까, 실제 전달한 인수는 argc -1 개가 되는 거임. 
    fprintf(stderr, "usage: %s <port>\n", argv[0]);   // 잘못 실행 했을때 , 포트 번호를 명령줄 인수로 전달해야 한다는 것을 설명
    exit(1);   // 비정상 종료하는 함수, 1은 에러, 0은 성공.
  }

  listenfd = Open_listenfd(argv[1]);  // 포트 번호를 사용해 리슨 소켓을 열고, 파일 디스크립터를 반환.
  while (1) {   // 서버는 무한루프에 진입하여 계속해서 클라이언트의 연결 요청을 기다린다.
    clientlen = sizeof(clientaddr);
    

    // accept 함수 : 클라이언트의 연결 요청을 기다리다가 요청이 오면 수락하고, 새로운 소켓 fd를 반환 
    // 서버가 요청 수신하는데 사용되는 리슨 소켓의 fd // 클라이언트의 소켓 주소를 저장할 구조체 // 구조체의 크기를 나타내는 변수의 주소.
    clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);  // 클라이언트에서 받아온거

    // 클라이언트의 소켓 주소 정보를 사람이 읽을 수 있는 형식으로 변환하는 함수, IP주소와 포트 번호를 문자열로 반환 해줌.
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);  // 마지막 0 : 플러그를 설정하지 않기 위해 0을 사용함.

    printf("Accepted connection from (%s, %s)\n", hostname, port);  // 연결된 클라이언트의 호스트 이름과 포트 번호를 출력함.
    doit(clientfd);  // 이 함수가 HTTP 요청을 읽음. 
    Close(clientfd);  // 클라이언트와의 연결이 끝나면 연결 소켓을 닫음. 이후 새로운 클라이언트 연결 요청을 기다림.
  }
  return 0;
}

/* 클라이언트가 보낸 HTTP요청을 분석 */
void doit(int clientfd)
{
  // 클라이언트로부터 읽은 HTTP 요청 라인을 저장하는 버퍼, HTTP 요청 라인에서 요청방식, URI, 버전 정보를 각각 저장.
  char buf_cl[MAXLINE], buf_sv[MAXLINE], method[MAXLINE], uri[MAXLINE], host_name[MAXLINE], url_path[MAXLINE], version[MAXLINE], port[MAXLINE];
  char *tiny_ptr;

  int tinyfd, content_len;
  rio_t rio_cl, rio_sv;  // *robust I/O*에서 사용하는 읽기 *버퍼* 구조체.

  Rio_readinitb(&rio_cl, clientfd);  // 브라우저와 연결된 읽기 버퍼를 초기화(init) 한줄씩 읽어가는 상태를 처음 상태로 초기화함.
  Rio_readlineb(&rio_cl, buf_cl, MAXLINE);  // 클라이언트로부터 한 줄의 요청 라인을 읽어옴. 결과는 buf에 저장됨.
  sscanf(buf_cl, "%s %s %s", method, uri, version);  // 요청라인에서 각각 추출해서 저장함.
  parse_uri(uri, host_name, port, url_path);

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) {
    clienterror(clientfd, method, "501", "Not implemented", "Tiny does not implement this method");   // GET, HEAD이외의 메소드를 사용하면 클라이언트에게 메세지를 보냄.
    return;
  }
  tinyfd = Open_clientfd(host_name, port);  // tiny와 연결될 fd (proxy가 연결하려는 tiny의 호스트네임.)

  if (tinyfd < 0) { // 연결 실패 시
    clienterror(clientfd, host_name, "404", "Not found", "Proxy couldn't connect to the server"); // 클라이언트에게 오류 응답 전송
    return;
  }
  // 요청 라인을 처리하는 함수, 클라이언트가 요청한 사항을 tiny에 보냄.
  sprintf(buf_cl, "%s %s HTTP/1.0\r\n", method, url_path);   //HTTP 요청 라인에는 호스트 이름이나 포트 번호가 들어가면 안됨. 요청헤더에서 따로 지정됨!!
  Rio_writen(tinyfd, buf_cl, strlen(buf_cl));

  // 요청 헤더 서버에 보내는 함수.
  read_requesthdrs(&rio_cl, tinyfd, host_name);  

  // 응답 헤더 : tiny에서 받아오고 client로 보내주는 작업.
  Rio_readinitb(&rio_sv, tinyfd);  // tiny와 연결된 읽기 버퍼를 초기화(init) 한줄씩 읽어가는 상태를 처음 상태로 초기화함.
  Rio_readlineb(&rio_sv, buf_sv, MAXLINE);  // 클라이언트로부터 한 줄의 요청 라인을 읽어옴. 결과는 buf에 저장됨.

  while(strcmp(buf_sv, "\r\n")) {  // '\r\n'(빈줄 : 응답의 끝)이 나올 때까지 계속 읽음. -> 같아지면 0이 반환되서 false
    if (strstr(buf_sv, "Content-length")) // HTTP 응답 헤더 확인.
      content_len = atoi(strchr(buf_sv, ':') + 1);  // 문자열을 정수로 변환하는 함수 atoi -> :을 가리키고 그 이후의 문자열을 정수로 변환.
    Rio_writen(clientfd, buf_sv, strlen(buf_sv));
    Rio_readlineb(&rio_sv, buf_sv, MAXLINE);  // 클라이언트로부터 한 줄의 요청 라인을 읽어옴. 결과는 buf에 저장됨.
  }
  Rio_writen(clientfd, buf_sv, strlen(buf_sv));

  // 응답 본문 : tiny에서 받아오고 client로 보내주는 작업.
  tiny_ptr = malloc(content_len);   // 본문을 저장할 메모리 시작주소를 포인터에 저장.
  Rio_readnb(&rio_sv, tiny_ptr, content_len);   // rio_sv에서 ptr에 본문데이터를 content_len 만큼 읽어 오는 함수.
  Rio_writen(clientfd, tiny_ptr, content_len);  // 클라이언트에 전송.
  free(tiny_ptr);

  // 다 끝났으니 타이니fd는 연결 종료.
  Close(tinyfd);
}


/* rp(rio_t구조체)를 이용해 rio_readlineb 함수를 통해 HTTP 요청 헤더들을 읽고 서버에 보내는 역할.
 * 요청 라인은 클라이언트가 서버에 무엇을 요청하는지 설명하고, 헤더들은 추가적인 정보를 제공한다. */
void read_requesthdrs(rio_t *rp, int fd, char *host_name)
{
  char buf[MAXLINE];  // 버퍼: 한 줄씩 읽어 저장할 공간.

  sprintf(buf, "Host: %s\r\n", host_name);  
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%s", user_agent_hdr);  
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Connection: close\r\n");  
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Proxy-Connection: close\r\n");  
  Rio_writen(fd, buf, strlen(buf));
 
  Rio_readlineb(rp, buf, MAXLINE);  // While 조건문이 일단 buf에 한줄이 있어야 하기 때문에 미리 while문 밖에서 한줄 받음.

  while(strcmp(buf, "\r\n")) {  // '\r\n'(빈줄 : 헤더의 끝)이 나올 때까지 계속 요청 헤더를 읽음. -> 같아지면 0이 반환되서 false

    if (strstr(buf, "Host") || strstr(buf, "User-Agent") || strstr(buf, "Connection") || strstr(buf, "Proxy-Connection")) {
      Rio_readlineb(rp, buf, MAXLINE);
      continue;
    }
    Rio_writen(fd, buf, strlen(buf));
    Rio_readlineb(rp, buf, MAXLINE);
  }
  sprintf(buf, "\r\n");
  Rio_writen(fd, buf, strlen(buf));
  return;
}

/* 클라이언트가 요청한 *URI*를 분석해서 호스트 네임과 경로를 분리한다. */ 
void parse_uri(char *uri, char *host_name, char *port, char *url_path)
{
  char *name_ptr, *path_ptr, *port_ptr;  // URI에서 특정 문자를 찾을 때 사용할 포인터 변수.

  // host_name 시작 시점 찾기.
  name_ptr = strstr(uri, "://");  // 만약 http://가 있으면 Null이 아니라 :의 위치주소를 반환함.
  if(name_ptr != NULL) {   
    name_ptr += 3;  // '/' 다음부터가 host_name이니까.
  } else {
    name_ptr = uri;   // "://"이 없으면 URI 처음부터가 host_name.
  }

  // url_path 저장
  path_ptr = strchr(name_ptr, '/');   // 처음 '/'가 나올때의 위치
  if (path_ptr != NULL) 
    strcpy(url_path, path_ptr);
  else
    strcpy(url_path, '/');



  // port 번호 고려해서 host_name 저장
  port_ptr = strchr(name_ptr, ':');
  if(port_ptr) {
    strncpy(host_name, name_ptr, port_ptr-name_ptr);  // host_name에 두번째부터 세번째 인자까지 넣음.
    strncpy(port, port_ptr + 1, path_ptr-port_ptr - 1);   // ':' 이후부터 '/' 전 까지 포트번호 복사.

  } else {
    strncpy(host_name, name_ptr, path_ptr-name_ptr);  // host_name에 두번째(시작점)부터 세번째 인자만큼 넣음.
    strcpy(port, "80");
  }
  
  
}
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
// fd : 클라이언트와 연결을 나타내는 fd, cause: 에러의 원인을 설명하는 문자열, errnum : 에러코드
// 짧은 에러 상태 설명, 긴 에러 메세지는 자세한 설명을 제공
{
  char buf[MAXLINE], body[MAXBUF];

  // sprintf: 문자열을 포맷화해서 버퍼에 저장하는 함수, body: 에러 페이지의 HTML 코드가 저장될 버퍼.
  // 각 sprintf 호출은 body 버퍼에 HTML 페이지의 다른 부분을 추가.
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);   // 배경 색 흰색
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // HTTP 응답 헤더 생성하고 클라이언트로 전송
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);   // 예를 들어 HTTP/1.0 404 Not Found
  Rio_writen(fd, buf, strlen(buf));   // 위에서 생성된 문자열들을 클라이언트로 전송한다.
  sprintf(buf, "Content-type: text/html\r\n");  // 클라이언트가 응답이 HTML 형식임을 알 수 있게 함.
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));  // 응답 본문(HTML)의 길이를 설정.
  Rio_writen(fd, buf, strlen(buf));

  Rio_writen(fd, body, strlen(body));   // 마지막으로 HTML 형식으로 작성된 에러 페이지를 클라이언트에게 전송함.
}