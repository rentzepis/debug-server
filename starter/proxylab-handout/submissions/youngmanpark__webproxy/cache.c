#include "cache.h"
#include "csapp.h"
#include <stdio.h>

web_object *root;
web_object *tail;
int total_cache_size = 0;

/* 캐싱 데이터중 filename 웹 객체 반환 함수 */
web_object *find_cache(char *filename) {

    // 캐시가 비었다면
    if (!root)
        return NULL;

    web_object *now = root;
    while (strcmp(now->filename, filename)) {
        if (now->next == NULL)
            return NULL;
        now = now->next;
        if (strcmp(now->filename, filename) == 0)
            return now;
    }
    return now;
}

/* 클라이언트에게 캐싱 데이터 전송 함수 */
void send_cache(web_object *web_ob, int fd) {
    char buf[MAXLINE]; // 임시 버퍼

    // header 전송
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, web_ob->content_len);
    Rio_writen(fd, buf, strlen(buf));

    // body 전송
    Rio_writen(fd, web_ob->response_ptr, web_ob->content_len);
}

/* 전송 후 캐싱 리스트 재정렬 함수 */
void update_cache(web_object *web_ob) {

    // 객체가 캐시리스트의 맨처음일 경우
    if (web_ob == root)
        return;
    // 캐시리스트에서 객체가 마지막이 아닐 경우
    if (web_ob->next != NULL) {
        web_object *prev_ob = web_ob->prev; // 객체의 이전 객체
        web_object *next_ob = web_ob->next; // 객체의 다음 객체

        // 이전 객체가 없을 경우
        if (prev_ob != NULL)
            web_ob->prev->next = next_ob;
        else
            web_ob->next->prev = prev_ob;
    }
    // 캐시리스트에서 객체가 마지막일 경우
    else {
        web_ob->prev->next = NULL;
    }

    web_ob->next = root; // 기존의 맨처음 객체(root)는 현재 객체의 다음 객체로(web_ob->next)
    root = web_ob;       // 캐시리스트의 맨처음 객체(root)로 현재 객체(web_ob) 설정
}

/* 웹 객체 캐싱 리스트에 등록 함수*/
void regi_cache(web_object *web_ob) {
    total_cache_size += web_ob->content_len;

    while (total_cache_size > MAX_CACHE_SIZE) {
        total_cache_size -= tail->content_len; // 총 캐시 리스트의 크기를 마지막 객체의 크기만큼 빼기
        tail = tail->prev;                     // 마지막 캐시를 마지막 이전 캐시로 변경
        free(tail->next->response_ptr);
        free(tail->next);                      // 제거한 캐시데이터 메모리 반환
        tail->next = NULL;                     // 변경된 마지막 캐시 데이터의 다음을 NULL(끝임을 알려줌)로 변경
    }
    // 빈 캐시리스트일 경우 root,tail에 객체 담기
    if (root == NULL) {
        tail = web_ob;
        root = web_ob;
    }
    // 빈 캐시리스트가 아닐 경우, 객체를 root로 설정
    else {
        web_ob->next = root; // 기존의 맨처음 객체(root)는 현재 객체의 다음 객체로(web_ob->next)
        root->prev = web_ob; // 현재 객체(web_ob)를 기존의 맨처음 객체(root)의 이전 객체로(web_ob->prev)
        root = web_ob;       // 캐시리스트의 맨처음 객체(root)로 현재 객체(web_ob) 설정
    }
}
