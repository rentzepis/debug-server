#include "cache.h"

#include <stdio.h>

#include "csapp.h"

web_object_t *rootp;
web_object_t *lastp;
int total_cache_size = 0;

web_object_t *find_cache(char *path) {
    if (!rootp)
        return NULL;
    web_object_t *current = rootp;
    while (strcmp(current->path, path)) {
        if (!current->next)
            return NULL;

        current = current->next;
        if (!strcmp(current->path, path))
            return current;
    }
    return current;
}

void send_cache(web_object_t *web_object, int clientfd) {
    char buf[MAXLINE];
    sprintf(buf, "HTTP/1.0 200 OK\r\n");
    sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
    sprintf(buf, "%sConnection: close\r\n", buf);
    sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, web_object->content_length);
    Rio_writen(clientfd, buf, strlen(buf));

    Rio_writen(clientfd, web_object->response_ptr, web_object->content_length);
}

void read_cache(web_object_t *web_object) {
    if (web_object == rootp)
        return;

    if (web_object->next) {
        web_object_t *prev_objtect = web_object->prev;
        web_object_t *next_objtect = web_object->next;
        if (prev_objtect)
            web_object->prev->next = next_objtect;
        web_object->next->prev = prev_objtect;
    } else {
        web_object->prev->next = NULL;
    }

    web_object->next = rootp;
    rootp = web_object;
}

void write_cache(web_object_t *web_object) {
    total_cache_size += web_object->content_length;

    while (total_cache_size > MAX_CACHE_SIZE) {
        total_cache_size -= lastp->content_length;
        lastp = lastp->prev;
        free(lastp->next);
        lastp->next = NULL;
    }

    if (!rootp)
        lastp = web_object;
    else {
        web_object->next = rootp;
        rootp->prev = web_object;
    }
    rootp = web_object;
}