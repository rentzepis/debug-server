/* proxy.c - Runner for the single threaded proxy.
 * TEAM MEMBERS:
 *      Jai Manacsa
 *      Debbie Lim
 *      Utsav Bhandari
 */

#include "csapp.h"

#include "requests.h"
#include "blocklist.h"

// Sequential logging - log messages written in the order they are generated
#define LOGLIST_SEQUENTIAL
FILE *LOGLIST_SEQUENTIAL_FD = NULL;

// Logging for debugging purposes
#ifndef RELEASE
  #define LOGLIST_PRINTF
#endif

#define LOGLIST_IMPLEMENTATION

#include "loglist.h"

int main(int argc, char **argv)
{
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  int listen_fd  = Open_listenfd(argv[1]);

  FILE *logfile = fopen("proxy.log", "w");
  LOGLIST_SEQUENTIAL_FD = logfile;
  struct LogList *head = init_loglist();

  int blockfile = open("blocklist.txt", O_RDONLY);
  struct Blocklist list = {0};
  if (blockfile) {
    char *current = list.text;

    rio_t rio = {0};
    rio_readinitb(&rio, blockfile);

    int len = 0;
    while ((len = rio_readlineb(&rio, current, MAXLINE))) {
      list.sites[list.num_blocked] = current;
      list.sites_lens[list.num_blocked] = len - 1;
      list.num_blocked += 1;
      current += len;
    }
  }

  while (1) {
    struct sockaddr_in clientaddr = {0};
    socklen_t client_len = sizeof(clientaddr);
    int connection_fd = accept(listen_fd, (struct sockaddr *) &clientaddr, &client_len);
    char *client_ip = inet_ntoa(clientaddr.sin_addr); // @ThreadSafety ?

    char message[MAXLINE] = {0};
    int message_len = sprintf(message, "Accepted connection from %s\n", client_ip);
    log_message(head, message, message_len);

    handle_request(connection_fd, client_ip, list, head);
    Close(connection_fd);
  }

  close(listen_fd);
  close(blockfile);
  fclose(LOGLIST_SEQUENTIAL_FD);
  return 0;
}

