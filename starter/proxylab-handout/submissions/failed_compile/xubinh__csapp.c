#include "cache.h"
#include "csapp.h"
#include "http_request_parser.h"
#include "integer_queue.h"
#include "proxy_utils.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAX_THREAD_NUMBER 4
#define CACHE_SEGMENT_NUMBER 4
#define MAX_QUEUE_SIZE 1024
#define USER_AGENT_HDR "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n"
#define CONNECTION_HEADER "Connection: close\r\n"
#define PROXY_CONNECTION_HEADER "Proxy-Connection: close\r\n"
#define CONTENT_LENTH_FIELD_NAME "Content-length:"

typedef struct sockaddr_storage sockaddr_storage;
typedef struct sockaddr sockaddr;

static const char *user_agent_hdr = USER_AGENT_HDR;
static const size_t user_agent_hdr_length = (sizeof USER_AGENT_HDR) - 1;
static const char *connection_header = CONNECTION_HEADER;
static const size_t connection_header_length = (sizeof CONNECTION_HEADER) - 1;
static const char *proxy_connection_header = PROXY_CONNECTION_HEADER;
static const size_t proxy_connection_header_length = (sizeof PROXY_CONNECTION_HEADER) - 1;
static const char *content_lenth_field_name = CONTENT_LENTH_FIELD_NAME;
static const size_t content_lenth_field_name_length = (sizeof CONTENT_LENTH_FIELD_NAME) - 1;
static Cache *http_request_cache = NULL;
static IntegerQueue *connected_socket_queue = NULL;

static void *thread(void *vargp);

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(1);
    }

    int port_value = atoi(argv[1]);
    if (port_value <= 1024 || port_value >= 65536) {
        fprintf(stderr, "port number out of range, please choose port number in [1025, 65535]\n");
        exit(1);
    }

    printf("pid: %d\n", getpid());

    printf("tid: %lu\n", pthread_self());

    // 捕获并忽略 `write` 函数的 `SIGPIPE` 信号:
    signal(SIGPIPE, SIG_IGN);

    printf("捕获并忽略 `write` 函数的 `SIGPIPE` 信号\n");

    // 初始化工作队列:
    connected_socket_queue = (IntegerQueue *)Malloc(sizeof(IntegerQueue));
    integer_queue_initialize(connected_socket_queue, MAX_QUEUE_SIZE);

    printf("初始化工作队列\n");

    // 初始化缓存:
    http_request_cache = (Cache *)Malloc(sizeof(Cache));
    cache_initialize(http_request_cache, CACHE_SEGMENT_NUMBER, MAX_CACHE_SIZE, MAX_OBJECT_SIZE);

    printf("初始化缓存\n");

    // 初始化 HTTP 请求解析器:
    if (!initialize_http_request_parser()) {
        fprintf(stderr, "初始化 HTTP 解析器失败\n");
        exit(1);
    }

    printf("初始化 HTTP 请求解析器\n");

    // 创建监听描述符:
    int listen_socket_descriptor = Open_listenfd(argv[1]);

    printf("创建监听描述符: %d\n", listen_socket_descriptor);

    // 初始化线程池 (不需要回收):
    int thread_index;
    pthread_t tid;
    for (thread_index = 0; thread_index < MAX_THREAD_NUMBER; thread_index++) {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    printf("初始化线程池\n");

    printf("开始循环\n");

    // 不断循环处理客户端请求:
    sockaddr_storage client_socket_address_storage;
    socklen_t client_socket_address_storage_size = sizeof(sockaddr_storage);
    while (1) {
        printf("主线程正在等待任务...\n");

        // 监听客户端连接:
        int connected_socket = Accept(listen_socket_descriptor, (sockaddr *)(&client_socket_address_storage), &client_socket_address_storage_size);

        printf("主线程监听到一个客户端连接, 描述符: %d\n", connected_socket);

        // 将已连接套接字送入工作队列:
        integer_queue_put(connected_socket_queue, connected_socket);

        printf("主线程已将描述符送入工作队列\n");
    }

    // 释放 HTTP 解析器:
    free_http_request_parser();

    // 释放缓存:
    cache_free(http_request_cache);

    // 释放工作队列:
    integer_queue_free(connected_socket_queue);

    return 0;
}

static void on_http_request_parse_error(int connected_socket) {
    return; // [TODO]
}

static void get_object_key(HTTPParseResult *http_parse_result, char **key, size_t *key_length) {
    *key_length = http_parse_result->host_length + http_parse_result->path_length;
    *key = (char *)Malloc(sizeof(char) * (*key_length + 1));
    strcpy(*key, http_parse_result->host);
    strcat(*key, http_parse_result->path);
}

typedef struct ContentNode {
    void *buffer;
    size_t buffer_length;
    struct ContentNode *next_node;
} ContentNode;

static void free_content_linked_list(ContentNode *linked_list) {
    if (!linked_list) {
        return;
    }

    ContentNode *current_node = linked_list;
    ContentNode *next_node = current_node->next_node;
    while (1) {
        if (current_node->buffer) {
            Free(current_node->buffer);
        }
        Free(current_node);
        if (!next_node) {
            return;
        }
        current_node = next_node;
        next_node = current_node->next_node;
    }
}

/**
 * @brief 根据已解析的客户端 HTTP 请求创建代理服务器自己的 HTTP 请求
 *
 * @param http_parse_result 已解析的客户端 HTTP 请求
 * @param http_request_buffer 返回的存储代理服务器自己的 HTTP 请求的缓冲区
 * @param http_request_buffer_length 返回的存储代理服务器自己的 HTTP 请求的缓冲区的大小
 */
static void generate_http_request(HTTPParseResult *http_parse_result, void **http_request_buffer, size_t *http_request_buffer_length) {
    // Host - 若没有则添加
    // User-Agent - 总是添加
    // Connection: close
    // Proxy-Connection: close

    // 计算请求行的大小:
    size_t request_line_length = 3 + 1 + http_parse_result->path_length + 1 + 8 + 2; // "GET" + SP + PATH + SP + "HTTP/1.0" + "\r\n"

    // 计算所有请求头部的总大小:
    RequestHeaderNode *previous_header_node = http_parse_result->request_headers; // 必定不为 NULL
    RequestHeaderNode *current_header_node = previous_header_node->next_node;
    int has_host_field = FALSE;
    size_t total_request_header_length = 0;
    while (current_header_node) {
        // 如果遇到 `Host` 请求头部:
        if (strcasecmp(current_header_node->field_name, "Host") == 0) {
            // 做标记:
            has_host_field = TRUE;
        }

        // 如果遇到默认需要替换的头部:
        else if (strcasecmp(current_header_node->field_name, "User-Agent") == 0 || strcasecmp(current_header_node->field_name, "Connection") == 0 || strcasecmp(current_header_node->field_name, "Proxy-Connection") == 0) {
            // 从链表中摘出, 并丢弃:
            previous_header_node->next_node = current_header_node->next_node;
            Free(current_header_node->field_name);
            Free(current_header_node->field_value);
            Free(current_header_node);
            current_header_node = previous_header_node->next_node;

            continue;
        }

        // 更新总大小:
        total_request_header_length += current_header_node->field_name_length + 1 + current_header_node->field_value_length + 2; // header = field_name + ":" + field_value + "\r\n"

        previous_header_node = current_header_node;
        current_header_node = previous_header_node->next_node;
    }

    // 如果没有 `Host` 头部:
    if (!has_host_field) {
        // 手动添加长度:
        total_request_header_length += 5 + http_parse_result->host_length + 1 + http_parse_result->port_length + 2; // "Host:" + host + ":" + port + "\r\n"
    }

    // 加上三个固定头部以及结尾行的长度:
    total_request_header_length += user_agent_hdr_length;
    total_request_header_length += connection_header_length;
    total_request_header_length += proxy_connection_header_length;
    total_request_header_length += 2;

    // 缓冲区总长度:
    *http_request_buffer_length = request_line_length + total_request_header_length;

    // 创建缓冲区:
    *http_request_buffer = Malloc((*http_request_buffer_length) + 1);

    // 开始填充 HTTP 请求:
    char *current_http_request_buffer_position = *http_request_buffer;

    // 填充请求行:
    strcpy(current_http_request_buffer_position, "GET ");
    current_http_request_buffer_position += 4;
    strcat(current_http_request_buffer_position, http_parse_result->path);
    current_http_request_buffer_position += http_parse_result->path_length;
    strcat(current_http_request_buffer_position, " HTTP/1.0\r\n");
    current_http_request_buffer_position += 11;

    // 填充所有请求头部:
    previous_header_node = http_parse_result->request_headers;
    current_header_node = previous_header_node->next_node;
    while (current_header_node) {
        strcat(current_http_request_buffer_position, current_header_node->field_name);
        current_http_request_buffer_position += current_header_node->field_name_length;

        strcat(current_http_request_buffer_position, ":");
        current_http_request_buffer_position += 1;

        strcat(current_http_request_buffer_position, current_header_node->field_value);
        current_http_request_buffer_position += current_header_node->field_value_length;

        strcat(current_http_request_buffer_position, "\r\n");
        current_http_request_buffer_position += 2;

        previous_header_node = current_header_node;
        current_header_node = previous_header_node->next_node;
    }

    // 添加固定的几个头部:
    if (!has_host_field) {
        strcat(current_http_request_buffer_position, "Host:");
        current_http_request_buffer_position += 5;

        strcat(current_http_request_buffer_position, http_parse_result->host);
        current_http_request_buffer_position += http_parse_result->host_length;

        strcat(current_http_request_buffer_position, ":");
        current_http_request_buffer_position += 1;

        strcat(current_http_request_buffer_position, http_parse_result->port);
        current_http_request_buffer_position += http_parse_result->port_length;

        strcat(current_http_request_buffer_position, "\r\n");
        current_http_request_buffer_position += 2;
    }

    strcat(current_http_request_buffer_position, user_agent_hdr);
    current_http_request_buffer_position += user_agent_hdr_length;

    strcat(current_http_request_buffer_position, connection_header);
    current_http_request_buffer_position += connection_header_length;

    strcat(current_http_request_buffer_position, proxy_connection_header);
    current_http_request_buffer_position += proxy_connection_header_length;

    // 结尾行:
    strcat(current_http_request_buffer_position, "\r\n");
    current_http_request_buffer_position += 2;

    *current_http_request_buffer_position = '\0';
}

static int get_payload_size_from_object(void *object, size_t *payload_size) {
    char *http_response = (char *)object;
    char *content_length_field_name_position = strstr(http_response, content_lenth_field_name);
    if (!content_length_field_name_position) {
        return FALSE;
    }

    *payload_size = atoi(content_length_field_name_position + content_lenth_field_name_length);
    if (!(*payload_size)) {
        return FALSE;
    }

    return TRUE;
}

static int get_object_from_original_server(HTTPParseResult *http_parse_result, void **object, size_t *object_size, size_t *payload_size) {
    // 生成代理服务器自己的 HTTP 请求:
    void *http_request_buffer;
    size_t http_request_buffer_length;
    generate_http_request(http_parse_result, &http_request_buffer, &http_request_buffer_length);

    printf("生成代理服务器自己的 HTTP 请求, 内容:\n%s\n", (char *)http_request_buffer);

    // 打开与服务器的连接并发送请求:
    int server_socket = Open_clientfd(http_parse_result->host, http_parse_result->port);

    // 如果服务器提前关闭连接:
    if (Rio_writen(server_socket, http_request_buffer, http_request_buffer_length) < 0) {
        Free(http_request_buffer);
        Close(server_socket);

        printf("服务器提前关闭连接\n");

        return FALSE;
    }

    printf("成功向服务器发送请求\n");

    // 初始化 RIO 缓冲区:
    rio_t *rio = (rio_t *)Malloc(sizeof(rio_t));
    Rio_readinitb(rio, server_socket);

    printf("初始化 RIO 缓冲区\n");

    // 初始化服务器数据流缓冲区:
    ContentNode linked_list;
    linked_list.next_node = NULL;
    ContentNode *tail_node = &linked_list;

    *object_size = 0;

    // 不断读取服务器返回的数据流:
    while (1) {
        ContentNode *new_node = (ContentNode *)Malloc(sizeof(ContentNode));
        new_node->next_node = NULL;
        new_node->buffer = Malloc(MAXLINE + 1);

        printf("初始化一个新的服务器数据流缓冲区\n");

        new_node->buffer_length = Rio_readnb(rio, new_node->buffer, MAXLINE);

        printf("读取到从服务器返回的数据流\n");

        // 如果遇到 EOF:
        if (new_node->buffer_length == 0) {
            // 释放当前新结点已分配的缓冲区:
            Free(new_node->buffer);
            Free(new_node);

            Close(server_socket);

            printf("遇到 EOF, 关闭与服务器的连接\n");

            break;
        }

        // 如果连接被提前关闭:
        else if (new_node->buffer_length < 0) {
            // 释放当前新结点已分配的缓冲区:
            Free(new_node->buffer);
            Free(new_node);

            // 释放已经积累的所有结点:
            free_content_linked_list(linked_list.next_node);

            Free(rio);

            Free(http_request_buffer);

            Close(server_socket);

            printf("服务器连接被提前关闭\n");

            return FALSE;
        }

        ((char *)new_node->buffer)[new_node->buffer_length] = '\0';

        // 挂到链表尾部:
        tail_node->next_node = new_node;
        tail_node = new_node;

        // 更新响应的总大小:
        *object_size += new_node->buffer_length;

        // printf("数据流非空, 内容:\n%s\n", (char *)new_node->buffer);
        printf("数据流非空\n");
    }

    printf("成功读取服务器响应\n");

    // 创建整个响应缓冲区:
    *object = Malloc((*object_size) + 1);

    // 复制所有数据:
    void *current_object_position = *object;
    ContentNode *current_node = linked_list.next_node;
    while (current_node) {
        memcpy(current_object_position, current_node->buffer, current_node->buffer_length);
        current_object_position += current_node->buffer_length;
        current_node = current_node->next_node;
    }

    // 以防万一的合理性检查:
    if (!get_payload_size_from_object(*object, payload_size)) {
        Free(*object);

        free_content_linked_list(linked_list.next_node);

        Free(rio);

        Free(http_request_buffer);

        printf("没有在服务器响应中找到 Content-length 头部或内容长度为 0\n");

        return FALSE;
    }

    free_content_linked_list(linked_list.next_node);

    Free(rio);

    Free(http_request_buffer);

    return TRUE;
}

static void send_object_back_to_client(int connected_socket, void *object, size_t object_size) {
    // 不论客户端是否提前关闭都认为发送成功:
    Rio_writen(connected_socket, object, object_size);
}

static void *thread(void *vargp) {
    printf("pid: %d\n", getpid());

    printf("tid: %lu\n", pthread_self());

    Pthread_detach(pthread_self());

    printf("副线程分离\n");

    printf("副线程开始循环\n");

    HTTPParseResult http_parse_result;

    while (1) {
        printf("副线程正在等待任务...\n");

        // 从工作队列中获取任务:
        int connected_socket = integer_queue_pop(connected_socket_queue);

        printf("副线程已接受一个客户端 HTTP 请求, 描述符: %d\n", connected_socket);

        // 初始化 HTTP 请求对象:
        initialize_http_parse_result(&http_parse_result);

        printf("初始化 HTTP 请求对象\n");

        // 如果读取失败:
        if (!http_request_parser(connected_socket, &http_parse_result)) {
            on_http_request_parse_error(connected_socket);

            printf("HTTP 请求读取失败\n");

            free_http_parse_result(&http_parse_result);

            Close(connected_socket);

            continue;
        }

        // 如果不是 `GET` 方法:
        if (strcmp(http_parse_result.method, "GET") != 0) {
            on_http_request_parse_error(connected_socket);

            printf("不支持的场景: 不是 `GET` 方法\n");

            free_http_parse_result(&http_parse_result);

            Close(connected_socket);

            continue;
        }

        // 如果 HTTP 版本不受支持:
        if (strcmp(http_parse_result.http_version, "HTTP/1.0") != 0 && strcmp(http_parse_result.http_version, "HTTP/1.1") != 0) {
            on_http_request_parse_error(connected_socket);

            printf("不支持的场景: HTTP 版本不受支持\n");

            free_http_parse_result(&http_parse_result);

            Close(connected_socket);

            continue;
        }

        char *key = NULL;
        size_t key_length;
        void *object = NULL;
        size_t object_size;
        size_t payload_size;

        // 生成客户端所请求对象的键:
        get_object_key(&http_parse_result, &key, &key_length);

        printf("生成客户端所请求对象的键\n");

        // 如果所请求的是静态内容:
        if (!http_parse_result.is_dynamic) {
            printf("所请求的是静态内容, 尝试从缓存中获取对象\n");

            // 尝试获取对象:
            int cache_get_status = cache_get(http_request_cache, key, &object, &object_size);

            // 如果存在缓存:
            if (cache_get_status) {
                // 直接发送回客户端:
                send_object_back_to_client(connected_socket, object, object_size);

                printf("存在缓存, 直接发送回客户端\n");

                Free(object);

                Free(key);

                free_http_parse_result(&http_parse_result);

                Close(connected_socket);

                continue;
            }

            printf("不存在缓存\n");
        }

        // 如果所请求的是静态内容但没有缓存, 或者所请求的是动态内容, 则从服务器首次获取对象:
        get_object_from_original_server(&http_parse_result, &object, &object_size, &payload_size);

        printf("成功从目标服务器获取对象, 对象有效载荷大小: %lu\n", payload_size);

        // 如果是静态对象则尝试存入缓存:
        if (!http_parse_result.is_dynamic) {
            printf("所请求的是静态内容, 尝试存入缓存\n");

            cache_put(http_request_cache, key, key_length, object, object_size, payload_size);

            printf("已存入缓存\n");
        }

        // 将对象发送回客户端:
        send_object_back_to_client(connected_socket, object, object_size);

        printf("将对象发送回客户端\n");

        Free(object);

        printf("释放 object 指针\n");

        Free(key);

        printf("释放 key 指针\n");

        free_http_parse_result(&http_parse_result);

        printf("释放 http_parse_result 数据结构\n");

        Close(connected_socket);

        printf("客户端连接已关闭\n");
    }

    return NULL;
}