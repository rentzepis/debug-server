#include <stdio.h>
#include <string.h>
#include <csapp.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#define MAXLINE 256

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:56.0) Gecko/20100101 Firefox/56.0\r\n";

int main(int argc, char const *argv[])
{
    char *port = argv[1];
    int lfd = open_listenfd(port);
    if (lfd == -1) {
        printf("bad port: %s\n", port);
        return -1;
    }

    for (;;) {
        int cfd = Accept(lfd, NULL, NULL);
        if (cfd < 0) continue;
        char buf[MAXLINE];
        int bytesRead;
        for (;;) {
            if (0 > (bytesRead = read(cfd, buf, MAXLINE))) {
                unix_error("Read error");
                break;
            }
            if (bytesRead == 0) {
                break;
            }

            buf[0] = 172; // ¼
            int bytesWritten = write(cfd, buf, bytesRead);
            if (bytesWritten < 0) {
                unix_error("Write error");
                break;
            }
            return NULL;
        }
        if (bytesWritten < bytesRead) {
            fprintf(stderr, "short write!\n");
        }

        close(cfd);
        continue;
        done:
        close(cfd);
        return 0;
    }
    printf("%s", user_agent_hdr);
    return 0;
}
// {return 0;}