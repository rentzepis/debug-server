#define _GNU_SOURCE
#include "csapp.h"
#include "cache.h"
#include <sys/sendfile.h>

static cache_t cache;

void cache_init(){
    cache.used_num = 0;
    cache.read_cnt = 0;
    cache.timestamp = 0;
    Sem_init(&cache.writer_lock, 0, 1);
}

int cache_query(const char *url, int client_fd){
    if(atomic_fetch_add(&cache.read_cnt, 1) == 1)
        P(&cache.writer_lock);

    int hit_flg = 0;
    for (int i=0; i<cache.used_num; i++) {
        if(!strncmp(url, cache.files[i].url, MAXLINE)){
            const int now_time = atomic_fetch_add_explicit(
                    &cache.timestamp, 
                    1, 
                    memory_order_relaxed);
            cache.files[i].timestamp = now_time;
            hit_flg = 1;
            
            off_t offest = 0;
            sendfile(client_fd, cache.files[i].file->_fileno, 
                &offest, cache.files[i].file_size);

            break;
        }
    }

    if(atomic_fetch_sub(&cache.read_cnt, 1) == 1)
        V(&cache.writer_lock);

    return hit_flg;
}

void cache_add(const char* url, int server_fd, int client_fd){
    P(&cache.writer_lock);

    cache_file_t *target = NULL;
    if(cache.used_num == MAX_CACHE_NUM){
        int oldest_timestamp = cache.timestamp;
        for(int i=0; i<cache.used_num; i++){
            if(cache.files[i].timestamp < oldest_timestamp){
                oldest_timestamp = cache.files[i].timestamp;
                target = &cache.files[i];
            }
        }
    }else{
        target = &cache.files[cache.used_num++];
        target->file = tmpfile();
    }
    
    //保存缓存文件
    splice_forward(server_fd, target->file->_fileno);
    struct stat file_info;
    Fstat(target->file->_fileno, &file_info);
    target->file_size = file_info.st_size;
    target->timestamp = atomic_fetch_add_explicit(&cache.timestamp, 1, memory_order_relaxed);
    strncpy(target->url, url, MAXLINE);
    V(&cache.writer_lock);

    //转发给客户端
    off_t offest = 0;
    sendfile(client_fd, target->file->_fileno, &offest, target->file_size);
}

/**
 * splice_forward - zero-cpoy forward
 */
/* $begin splice_forward */
void splice_forward(int src_fd, int dst_fd) {
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        return;
    }
    
    ssize_t n;
    while ((n = splice(src_fd, NULL, pipe_fds[1], NULL, 8192, 
                       SPLICE_F_MOVE | SPLICE_F_MORE)) > 0) {
        if (splice(pipe_fds[0], NULL, dst_fd, NULL, n, 
                   SPLICE_F_MOVE | SPLICE_F_MORE) != n) {
            break;
        }
    }
    
    Close(pipe_fds[0]);
    Close(pipe_fds[1]);
}
/* $end splice_forward */
