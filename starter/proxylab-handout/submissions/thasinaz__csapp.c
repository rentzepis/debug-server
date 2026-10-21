#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>

#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define T 4

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection_hdr = "Proxy-Connection: close\r\n";

struct cache_object
{
    atomic_int refcount;
    char target[MAXLINE];
    struct cache_object *prev;
    struct cache_object *next;
    size_t size;
    char buffer[];
};

struct cache
{
    pthread_rwlock_t lock;
    size_t size;
    struct cache_object *head;
    struct cache_object *tail;
};

static struct cache_object sentinel = {0, "", NULL, NULL, 0};
static struct cache cache = {PTHREAD_RWLOCK_INITIALIZER, 0, &sentinel, &sentinel};

void cache_write(struct cache_object* object)
{
    pthread_rwlock_wrlock(&cache.lock);
    if (object->prev == NULL && object->prev == NULL)
    {
        atomic_fetch_add(&object->refcount, 1);
        cache.size += object->size;
    }
    else
    {
        object->prev->next = object->next;
        object->next->prev = object->prev;
    }

    object->next = cache.head;
    cache.head->prev = object;
    cache.head = object;

    while (cache.size > MAX_CACHE_SIZE)
    {
        object = cache.tail->prev;
        cache.size -= object->size;
        if (object->prev != NULL)
        {
            object->prev->next = object->next;
        }
        else
        {
            cache.head = object->next;
        }
        object->next->prev = object->prev;
        object->prev = NULL;
        object->next = NULL;
        if (atomic_fetch_add(&object->refcount, -1) == 1)
        {
            Free(object);
        }
    }
    pthread_rwlock_unlock(&cache.lock);
}

struct cache_object *cache_read(char *target)
{
    pthread_rwlock_rdlock(&cache.lock);
    for (struct cache_object *cached_object = cache.head; cached_object != &sentinel; cached_object = cached_object->next)
    {
        if (strcmp(cached_object->target, target) == 0)
        {
            atomic_fetch_add(&cached_object->refcount, 1);
            pthread_rwlock_unlock(&cache.lock);
            cache_write(cached_object);
            return cached_object;
        }
    }
    pthread_rwlock_unlock(&cache.lock);
    return NULL;
}

void *doit(void *args)
{
    int listenfd = (int)(size_t)args;
    int connfd;
    while ((connfd = Accept(listenfd, NULL, 0)) >= 0)
    {
        struct cache_object *object = (struct cache_object *)Malloc(sizeof(struct cache_object) + MAX_OBJECT_SIZE);
        atomic_init(&object->refcount, 1);
        object->size = MAX_OBJECT_SIZE;
        object->prev = NULL;
        object->next = NULL;
        char *target = object->target;

        rio_t rio;
        Rio_readinitb(&rio, connfd);
        char _buf[MAXLINE];
        char *buf = _buf;
        ssize_t n = Rio_readlineb(&rio, buf, MAXLINE);
        if (strncmp(buf, "GET http://", 11) != 0 || strcmp(buf + n - 2, "\r\n") != 0)
        {
            goto close_connfd;
        }
        buf += 11;

        char _hostname[MAXLINE];
        char _port[MAXLINE] = "80";
        for (char *_target = _hostname;; buf++)
        {
            char ch = *buf;
            if (ch == '\0')
            {
                goto close_connfd;
            }

            if (ch == '/')
            {
                *_target = '\0';
                break;
            }

            if (ch == ':')
            {
                *_target = '\0';
                _target = _port;
            }
            else
            {
                *_target++ = ch;
            }

            *target++ = ch;
        }
        char *path = target;
        for (;; buf++)
        {
            char ch = *buf;
            if (ch == '\0')
            {
                goto close_connfd;
            }

            if (ch == ' ')
            {
                *target = '\0';
                break;
            }

            *target++ = ch;
        }
        if (path[0] == '\0')
        {
            goto close_connfd;
        }

        struct cache_object *cached_object = cache_read(object->target);
        if (cached_object != NULL)
        {
            Rio_writen(connfd, cached_object->buffer, cached_object->size);
            Free(object);
            object = cached_object;
            goto close_connfd;
        }

        int clientfd = Open_clientfd(_hostname, _port);
        Rio_writen(clientfd, "GET ", 4);
        Rio_writen(clientfd, path, strlen(path));
        Rio_writen(clientfd, " HTTP/1.0\r\n", 11);
        char _host_hdr[MAXLINE] = "Host: ";
        for (;;)
        {
            if ((n = Rio_readlineb(&rio, _buf, MAXLINE)) <= 0 || strcmp(_buf + n - 2, "\r\n") != 0)
            {
                goto close_connfd;
            }
            if (n == 2)
            {
                break;
            }

            if (strncmp(_buf, "Host: ", 6) == 0)
            {
                strcpy(_host_hdr + 6, _buf + 6);
                continue;
            }
            if (strncmp(_buf, "User-Agent: ", 12) == 0 ||
                strncmp(_buf, "Connection: ", 12) == 0 ||
                strncmp(_buf, "Proxy-Connection: ", 18) == 0)
            {
                continue;
            }
            Rio_writen(clientfd, _buf, n);
        }
        if (_host_hdr[6] == '\0')
        {
            memcpy(_host_hdr + 6, object->target, path - object->target);
            strcpy(_host_hdr + 6 + (path - object->target), "\r\n");
        }

        Rio_writen(clientfd, _host_hdr, strlen(_host_hdr));
        Rio_writen(clientfd, user_agent_hdr, strlen(user_agent_hdr));
        Rio_writen(clientfd, connection_hdr, strlen(connection_hdr));
        Rio_writen(clientfd, proxy_connection_hdr, strlen(proxy_connection_hdr));
        Rio_writen(clientfd, "\r\n", 2);

        size_t size = 0;
        while ((n = Rio_readn(clientfd, object->buffer + size % MAX_OBJECT_SIZE, MAX_OBJECT_SIZE - size % MAX_OBJECT_SIZE)) > 0)
        {
            Rio_writen(connfd, object->buffer, n);
            size += n;
        }
        close(clientfd);
        if (size > MAX_OBJECT_SIZE)
        {
            goto close_connfd;
        }

        object = (struct cache_object *)Realloc(object, sizeof(struct cache_object) + size);
        object->size = size;
        cache_write(object);

close_connfd:
        Close(connfd);
        if (atomic_fetch_add(&object->refcount, -1) == 1)
        {
            Free(object);
        }
    }
    return NULL;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int listenfd = Open_listenfd(argv[1]);
    pthread_t thread[T];
    for (int i = 0; i < T; i++)
    {
        Pthread_create(thread + i, NULL, doit, (void *)(ssize_t)listenfd);
    }
    for (int i = 0; i < T; i++)
    {
        Pthread_join(thread[i], NULL);
    }
    return 0;
}
