#ifndef CSAPP_PROXY_CACHE_H_
#define CSAPP_PROXY_CACHE_H_

#include <stddef.h>

void cache_init(size_t maxsize);

void cache_put(const char *host, const char *loc, const char *buf,
               size_t bufsz);

int cache_get(const char *host, const char *loc, char **buf, size_t *bufsz);

#endif /* CSAPP_PROXY_CACHE_H_ */
