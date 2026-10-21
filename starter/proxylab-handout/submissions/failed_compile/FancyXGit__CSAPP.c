#include "csapp.h"
#include "sbuf.h"
#include "cache.h"
#include <netdb.h>

#define MAX_STR_LENGTH 64
#define NTHREADS 4
#define SBUFSIZE 16

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
static const char *connection_hdr = "Connection: close\r\n";
static const char *proxy_connection = "Proxy-Connection: close\r\n";

sbuf_t sbuf;

/*
 * 按\r\n分割HTTP请求字符串
 * 分割的每一行存储在堆中
 * 会将result[i]改为指向堆中字符串的指针
 * request为传入HTTP字符串
 * result为传入的指向char*的数组
 * res_length为传入数组的长度
 * 返回读到多少有效行（最后一行\r\n）不算
 * 返回-1表示错误
 */
int SplitHttpLines(const char *request, char *result[], int res_length)
{
    // 开始循环分割
    const char *begin_line = request;               // 指向行头
    const char *end_line = strstr(request, "\r\n"); // 指向'\n'
    if (end_line == NULL)
    {
        return -1;
    }
    end_line++;
    int count = 0;
    while (end_line != NULL)
    {
        // 拷贝字符串一行进堆
        int size = (int)(end_line - begin_line + 1);         // 一行长度，不包括'\0'
        char *heap_line = malloc(sizeof(char) * (size + 1)); // 加上'\0'后的长度
        if (heap_line == NULL)
        {
            return -1;
        }
        memcpy(heap_line, begin_line, size); // 拷贝字符串进堆，不包括'\0'
        heap_line[size] = '\0';              // 加上'\0'在尾部

        // 设置result数组
        if (count >= res_length)
        {
            return -1;
        }
        result[count] = heap_line;
        count++;

        // 调整到下一行，进行迭代
        begin_line = end_line + 1;
        end_line = strstr(begin_line, "\r\n");
        if (end_line == NULL)
        {
            break;
        }
        end_line++;
    }
    // 找到最后一行'\r\n'改为NULL删去
    if (count <= 0)
    {
        return -1;
    }
    char *last_line = result[count - 1];
    if (strcmp("\r\n", last_line) == 0)
    {
        free(last_line);
        result[count - 1] = NULL;
        count--;
    }
    return count;
}

/*
 * 释放SplitHttpLines分配的堆空间
 */
void FreeLines(char *result[], int res_length)
{
    int i = 0;
    char *line = result[i];
    while ((line = result[i]) != NULL && (i < res_length))
    {
        free(line);
        i++;
    }
}

/*
 * 修改HTTP的起始行
 * "GET http://www.cmu.edu/hub/index.html HTTP/1.1" 修改为 "GET /hub/index.html HTTP/1.0"
 * 删除HOST头，将HTTP协议改成1.1
 * 同时返回堆上新分配的HOST头
 */
char *ModifyStartLine(char *start_line, int length)
{
    // 可能的情况
    // 1. 基本
    // GET http://www.cmu.edu/hub/index.html HTTP/1.1
    // -> GET /hub/index.html HTTP/1.0
    // 2. 无请求路径：添加/
    // GET http://www.cmu.edu HTTP/1.1
    // -> GET / HTTP/1.0
    // 3. 无请求路径但是有查询参数：添加/保留参数
    // GET http://www.cmu.edu?x=1 HTTP/1.1
    // -> GET /?x=1 HTTP/1.0
    // 4. 有请求路径有参数：保留路径和参数
    // GET http://www.cmu.edu/hub?x=1 HTTP/1.1
    // -> GET /hub?x=1 HTTP/1.0
    // 5. 有锚点：去除#及之后所有
    // GET http://www.cmu.edu/hub/index.html#section?lang=en HTTP/1.1
    // -> GET /hub/index.html HTTP/1.0
    // 6. 有用户名密码：去除用户名密码@
    // GET http://admin:123456@www.cmu.edu/hub/index.html HTTP/1.1
    // -> GET /hub/index.html HTTP/1.0
    // 7. 加入端口号：保留端口到HOST
    // GET http://www.cmu.edu:8080/hub/index.html HTTP/1.1
    // -> GET /hub/index.html HTTP/1.0
    // -> HOST: www.cmu.edu:8080
    // 8. @作为参数
    // GET http://admin:123456@www.cmu.edu:8080/hub/index.html?str=@nono#section?lang=en HTTP/1.1
    // -> GET /hub/index.html?str=@nono HTTP/1.0

    // HTTP协议改成1.0
    char *protocol_loc = strstr(start_line, "HTTP"); // protocol_loc指向HTTP的H，即协议版本的第一个字符
    if (protocol_loc == NULL)                        // 必须有HTTP
    {
        return NULL;
    }
    // 检查HTTP长度是否足够更改协议版本
    if ((int)(protocol_loc - start_line) + 8 > length)
    {
        return NULL;
    }
    // 修改协议版本
    protocol_loc[5] = '1';
    protocol_loc[6] = '.';
    protocol_loc[7] = '0';
    protocol_loc[8] = '\0';

    // 提取HOST
    char *http_loc = strstr(start_line, "http://"); // http_loc指向http://的h，即URL的第一个字符
    if (http_loc == NULL)                           // 必须有http://
    {
        return NULL;
    }
    // 处理@的情况
    char *at = strchr(http_loc, '@');
    // 从https://之后开始查找，遇到' ''?''/''#'终止
    char *host_end = strpbrk(http_loc + 7, " ?/#"); // 不为空，至少一定能找到空格
    if (host_end == NULL)
    {
        return NULL;
    }
    // host_end指向有效HOST的下一个字符
    char *host_start;
    if (at == NULL || at > host_end)
    {
        // 无admin:password@HOSTNAME情况
        // 或者@作为参数而非特殊字符
        host_start = http_loc + 7; // 跳到http://后面的字符，例如http://w跳到//后面的w
    }
    else
    {
        // 跳到@后面的字符
        host_start = at + 1;
    }
    // host_start指向有效HOST的第一个字符
    // 拷贝HOST到堆
    int size = (int)(host_end - host_start);
    char *host = malloc(sizeof(char) * (size + 1));
    if (host == NULL)
    {
        return NULL;
    }
    memcpy(host, host_start, size);
    host[size] = '\0';

    // 修改原字符串，去除HOST
    // 先拷贝路径
    // 计算长度
    char *p = host_end;
    while (*p != '#' && *p != ' ') // 遇见#与空格终止
    {
        p++;
    }
    int url_length = p - host_end;
    char *copy_start = http_loc;
    if (*host_end != '/')
    {
        // 不包含'/'添上
        *copy_start = '/';
        copy_start++;
    }
    // memcpy对重叠复制未定义，采用memmove
    memmove(copy_start, host_end, url_length);
    // 再拷贝HTTP协议
    copy_start += url_length;
    *copy_start = ' ';
    copy_start++;
    memmove(copy_start, protocol_loc, 8);
    copy_start[8] = '\r';
    copy_start[9] = '\n';
    copy_start[10] = '\0';

    return host;
}

#ifdef DEBUG

/*
 * 辅助函数，打印字符串数组
 */
void PrintLines(char *result[], int res_length)
{
    int i = 0;
    char *line;
    while ((i < res_length) && ((line = result[i]) != NULL))
    {
        printf("LINE:%d:\n%s\n", i + 1, line);
        i++;
    }
}

/*
 * 测试URL解析的函数
 */
void testModifyStartLine(void)
{
    char head[256];
    while (1)
    {
        fgets(head, 256, stdin);
        int length = strlen(head);
        head[length - 1] = '\0';
        char *res = ModifyStartLine(head, length - 1);
        if (res == NULL)
        {
            printf("解析失败！\n");
            continue;
        }
        printf("解析URL：%s\n", head);
        printf("解析HOST:%s\n", res);
        free(res);
    }
}
#endif

/*
 * 将分散存储的请求隔行合并到一起
 * 会固定设置HOST, User-Agent, Connection, Proxy-Connection
 * 其余字段保留不动
 */
int CombineHttp(char *buf, char **res, int count, char *host)
{
    int words = 0;
    // 1. 请求首行
    char *buff_ptr = buf;
    int size = strlen(res[0]); // 少加1不拷贝'\0'
    memcpy(buff_ptr, res[0], size);
    buff_ptr += size;
    words += size;
    // 2. Host
    memcpy(buff_ptr, "HOST: ", 6);
    buff_ptr += 6;
    size = strlen(host);
    memcpy(buff_ptr, host, size);
    buff_ptr += size;
    buff_ptr[0] = '\r'; // Host不带\r\n需要补充
    buff_ptr[1] = '\n';
    buff_ptr += 2;
    words += size + 8;
    // 3. User-Agent
    memcpy(buff_ptr, user_agent_hdr, 86); // 去除'\0'是86字符
    buff_ptr += 86;
    // 4. Connection
    memcpy(buff_ptr, connection_hdr, 19);
    buff_ptr += 19;
    // 5. Proxy-Connection
    memcpy(buff_ptr, proxy_connection, 25);
    buff_ptr += 25;
    words += 130; // 3-5
    // 6. 剩下的
    for (int i = 1; i < count; i++)
    {
        char *curr_head = res[i];
        char *colon = strchr(curr_head, ':');
        // 跳过无效行(没有冒号)
        if (colon == NULL)
        {
            continue;
        }
        int key_len = colon - curr_head;
        // 跳过前面的4个固定头
        if (strncasecmp(curr_head, "Host", key_len) == 0)
        {
            continue;
        }
        if (strncasecmp(curr_head, "User-Agent", key_len) == 0)
        {
            continue;
        }
        if (strncasecmp(curr_head, "Connection", key_len) == 0)
        {
            continue;
        }
        if (strncasecmp(curr_head, "Proxy-Connection", key_len) == 0)
        {
            continue;
        }
        // 复制头
        size = strlen(curr_head);
        memcpy(buff_ptr, curr_head, size);
        buff_ptr += size;
        words += size;
    }
    // 最后再加\r\n\0
    buff_ptr[0] = '\r';
    buff_ptr[1] = '\n';
    buff_ptr[2] = '\0';
    words += 3;
    return words;
}

/*
 * 从解析得到的Host中提取主机名和端口
 * 例如www.cmu.edu -> www.cmu.edu, 80
 * www.cmu.edu:8080 -> www.cmu.edu, 8080
 */
void getHostNamePort(char *host, int host_len, char *host_name, char *port)
{
    char *colon = strchr(host, ':');
    if (colon == NULL)
    {
        strcpy(host_name, host);
        strcpy(port, "80");
    }
    else
    {
        int host_name_len = (int)(colon - host);
        memcpy(host_name, host, host_name_len);
        host_name[host_name_len] = '\0';
        int port_size = host_len - host_name_len - 1;
        memcpy(port, colon + 1, port_size);
        port[port_size] = '\0';
    }
}

/*
 * 修改响应
 * 将Connection: keep-alive修改为Connection: close
 * 这个函数不必要
 */
void modifyResponse(char *response)
{
    char *connection_begin = strstr(response, "Connection");
    if (connection_begin != NULL)
    {
        char *connection_end = strstr(connection_begin, "\r\n");
        if (connection_end == NULL)
        {
            return;
        }
        connection_end += 2; // 指向下一行字段开始
        memcpy(connection_begin, "Connection: close\r\n", 19);
        int remain_len = strlen(connection_end);
        memmove(connection_begin + 19, connection_end, remain_len + 1); // 加1包括\0
    }
}

static void getCacheURL(const char* httpHead, char *res)
{
    char* blank1 = strchr(httpHead, ' ');
    blank1++;
    char *blank2 = strchr(blank1, ' ');
    int size = blank2 - blank1;
    memcpy(res, blank1, size + 1);
    res[size] = '\0';
}

/*
 * 提供代理服务
 * 不会关闭connfd
 */
void Service(int connfd)
{
    char buf[MAXLINE];
    rio_t rio_client;
    rio_t rio_server;

    Rio_readinitb(&rio_client, connfd);

    // 读请求
    buf[0] = '\0';
    while (1)
    {
        char line[MAXLINE];
        if (rio_readlineb(&rio_client, line, MAXLINE) <= 0)
        {
            break;
        }
        strcat(buf, line);
        if (strcmp(line, "\r\n") == 0)
        {
            break; // 空行 = 请求头结束
        }
    }
    // 分割请求行
    char *res[64];
    int count = SplitHttpLines(buf, res, 64);
    if (count <= 0)
    {
        goto srv_exit1;
    }

    // 解析缓存URL
    char url[64];
    char *cache_data;
    size_t cache_len;
    getCacheURL(res[0], url);
    // 查找缓存
    int cache_res = getCache(url, &cache_data, &cache_len);
    if (cache_res == 0)
    {
        // 缓存命中，直接返回
        rio_writen(connfd, cache_data, cache_len);
        goto srv_exit1;
    }

    // 缓存未命中
    // 解析请求首行
    char *host = ModifyStartLine(res[0], strlen(res[0])); // 得到的res每个字符串都有\r\n\0结尾
    if (host == NULL)
    {
        goto srv_exit1;
    }
    // 将请求排序并且依次复制到buf缓存区
    int req_size = CombineHttp(buf, res, count, host);

    // 提取host中的主机与域名
    char host_name[128];
    char port[16];
    getHostNamePort(host, strlen(host), host_name, port);

    // 发送请求
    int clientfd = open_clientfd(host_name, port);
    if (clientfd < 0)
    {
        goto srv_exit2;
    }
    rio_readinitb(&rio_server, clientfd);
    rio_writen(clientfd, buf, req_size);

    // 接受请求
    cache_data = malloc(sizeof(char) * MAX_OBJECT_SIZE);
    if (cache_data == NULL)
    {
        goto srv_exit3;
        return;
    }
    char *cache_data_ptr = cache_data;
    ssize_t cache_data_len = 0;
    ssize_t n;
    while ((n = rio_readnb(&rio_server, buf, MAXLINE)) != 0)
    {
        // 修改收到的请求
        // modifyResponse(buf);
        // 发送
        rio_writen(connfd, buf, n);
        cache_data_len += n;
        memcpy(cache_data_ptr, buf, n);
        cache_data_ptr += n;
    }
    // 存入缓存
    writeCache(url, cache_data, cache_data_len);
    free(cache_data);

srv_exit3:
    close(clientfd);
srv_exit2:
    free(host);
srv_exit1:
    FreeLines(res, count);
}

/*
 * 线程函数，传入vargp == NULL
 * 从connfd队列中取出
 * sbuf保证加锁
 */
void *thread(void *vargp)
{
    Pthread_detach(pthread_self());
    static pthread_once_t once = PTHREAD_ONCE_INIT;
    pthread_once(&once, CacheInit);

    while (1)
    {
        int connfd = sbuf_remove(&sbuf);
        Service(connfd);
        close(connfd);
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("参数不足\n");
        return 0;
    }
    int listenfd = Open_listenfd(argv[1]);

    int connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAX_STR_LENGTH];
    char client_port[MAX_STR_LENGTH];
    pthread_t tid;

    sbuf_init(&sbuf, SBUFSIZE);
    for (int i = 0; i < NTHREADS; i++)
    {
        Pthread_create(&tid, NULL, thread, NULL);
    }

    while (1)
    {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = accept(listenfd, (SA *)&clientaddr, &clientlen);
        if (connfd < 0)
        {
            continue;
        }
        if (getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAX_STR_LENGTH, client_port, MAX_STR_LENGTH, NI_NUMERICHOST | NI_NUMERICSERV) != 0)
        {
            continue;
        }
        printf("连接到 (%s, %s)\n", client_hostname, client_port);
        sbuf_insert(&sbuf, connfd);
    }
    return 0;
}
