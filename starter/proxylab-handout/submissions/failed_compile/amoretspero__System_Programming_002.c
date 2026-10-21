/*
 * proxy.c - CS:APP Web proxy
 *
 * Student ID: 2013-11438 
 *         Name: Hahm, Jiung
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 

#include "csapp.h"

/* The name of the proxy's log file */
#define PROXY_LOG "proxy.log"

/* Undefine this if you don't want debugging output */
#define DEBUG

/*
 * Functions to define
 */
void *process_request(void* vargp);
int open_clientfd_ts(char *hostname, int port, sem_t *mutexp);
ssize_t Rio_readn_w(int fd, void *ptr, size_t nbytes);
ssize_t Rio_readlineb_w(rio_t *rp, void *usrbuf, size_t maxlen);
void Rio_writen_w(int fd, void *usrbuf, size_t n);

void* thread(void* vargp);

typedef struct
{
	unsigned short port;
	struct in_addr addr;
	int fd;
}log_info;

sem_t mutex_oc;
sem_t mutex_log;
sem_t mutex_th;

char* days[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char* months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

char* get_host(char* str)
{
	//printf("DEBUG --- get_host : str - %s\n", str);
	//char* buf;
	char* host = strtok(str, " ");
	return host;
}

char* get_port(char* str)
{
	//printf("DEBUG --- get_port : str - %s\n", str);
	//char* buf;
	char* port = strtok(str, " ");
	//printf("get_port : port - %s\n", port);
	if (port == NULL) 
	{ 
		//printf("DEBUG --- port NULL!\n"); 
	}
	if (port != NULL)
	{
		port = strtok(NULL, " ");
	}
	return port;
}

char* get_message(char* str)
{
	//printf("DEBUG --- get_message : str - %s\n", str);
	//char* buf;
	char* msg = strtok(str, " ");
	if (msg != NULL)
	{
		msg = strtok(NULL, " ");
		if (msg != NULL)
		{
			msg = strtok(NULL, "");
		}
	}
	return msg;
}

char* ctos(char c)
{
	char* res = (char*)malloc(sizeof(char) * 2);
	res[0] = c;
	res[1] = '\0';
	//printf("ctos - res : %s\n", res);
	return res;
}

int host_checker (char* buf)
{
	//printf("DEBUG --- buf : %s\n", buf);
	if (buf == NULL || strcmp(buf, "\n") == 0)
	{
		return -1;
	}
	else
	{
		int cnt = 0;
		int len = strlen(buf);
		char res[128];
		strcpy(res, "");
		int dot_cnt = 0;
		for (cnt = 0; cnt < len && cnt < 15; cnt++)
		{
			//printf("res : %s, buf[cnt] : %s\n", res, ctos(buf[cnt]));
			if (buf[cnt] != ' ' && buf[cnt] != '\n')
			{
				char* tmp = ctos(buf[cnt]);
				strcat(res, tmp);
				//printf("after strcat\n");
			}
			else
			{
				break;
			}

			if (buf[cnt] == '.')
			{
				dot_cnt++;
			}
		}

		//printf("DEBUG --- res - host_checker : %s\n", res);

		if (strcmp(res, "localhost") == 0)
		{
			//printf("DEBUG --- res is localhost!\n");
			return 1;
		}
		else
		{
			//printf("DEBUG --- res is not localhost!\n");
			if (dot_cnt == 3)
			{
				return 1;
			}
			else
			{
				return -1;
			}
		}
	}
}

int port_checker (char* buf)
{
	//printf("DEBUG --- buf(port_checker) : %s\n", buf);
	if (buf == NULL || strcmp(buf, "\n") == 0)
	{
		return -1;
	}
	else
	{
		int cnt = 0;
		int space_cnt = 0;
		char res[128];
		strcpy(res, "");
		int len = strlen(buf);
		for(cnt = 0; cnt < len && cnt < 21; cnt++)
		{
			//printf("DEBUG --- cnt : %d, res : %s\n", cnt, res);
			if (space_cnt == 1)
			{
				if (buf[cnt] == '0' || buf[cnt] == '1' || buf[cnt] == '2' || buf[cnt] == '3' || buf[cnt] == '4' || buf[cnt] == '5' || buf[cnt] == '6' || buf[cnt] == '7' || buf[cnt] == '8' || buf[cnt] == '9')
				{
					char* temp = ctos(buf[cnt]);
					strcat(res, temp);
				}
				else
				{
					break;
				}
			}
			if (buf[cnt] == ' ')
			{
				space_cnt++;
			}
		}

		//printf("DEBUG --- res - port_checker : %s\n", res);

		if (res != NULL || strcmp(res, "") != 0)
		{
			return 1;
		}
		else
		{
			return -1;
		}
	}

}

/*
 * main - Main routine for the proxy program
 */
int main(int argc, char **argv)
{
		signal(SIGPIPE, SIG_IGN);
    /* Check arguments */
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
        exit(0);
    }
		Sem_init(&mutex_oc, 0, 1);
		Sem_init(&mutex_log, 0, 1);
		Sem_init(&mutex_th, 0, 1);

		//int clientfd, serverfd;
		int port_proxy;
		//int port_server;
		//char* host;
		//char buf[MAXLINE];
		//rio_t rio;

		port_proxy = atoi(argv[1]);

		struct sockaddr_in clientaddr;
		int clientlen = sizeof(clientaddr);
		int listenfd = Open_listenfd(port_proxy);

		//int proxy_fd;

		pthread_t tid;

		while(errno != EPIPE)
		{
			/*int* connfdp = Malloc(sizeof(int));
			struct hostent* hp;
			char* haddrp;
			int client_port;
			char* msg;
			rio_t rio;

			*connfdp = Accept(listenfd, (SA*)&clientaddr, &clientlen);

			// Establish connection.
			hp = Gethostbyaddr((const char*)&clientaddr.sin_addr.s_addr, sizeof(clientaddr.sin_addr.s_addr), AF_INET);
			haddrp = inet_ntoa(clientaddr.sin_addr);
			client_port = ntohs(clientaddr.sin_port);
			//printf("Proxy Server connected to %s (%s), port %d\n", hp->h_name, haddrp, client_port);
			
			//printf("DEBUG --- before Rio_readn_w\n");
			Rio_readn_w(*connfdp, buf, MAXLINE);
			printf("Proxy received from client : %s\n", buf);

			// Send to server.
			//clientaddr.sin_addr.s_addr = get_host(buf);
			port_server = atoi(get_port(buf));
			msg = get_message(buf);

			proxy_fd = Open_clientfd(get_host(buf), port_server);
			Rio_readinitb(&rio, proxy_fd);

			Rio_writen(proxy_fd, msg, strlen(msg));
			Rio_readlineb(&rio, msg, MAXLINE);
			printf("Proxy received from server : %s\n", msg);

			// Retrun to client. */
			int* connfd = (int*)malloc(sizeof(int));
			*connfd = Accept(listenfd, (SA*)&clientaddr, &clientlen);
			//printf("DEBUG --- Proxy server - accepted!\n");
			log_info* log = (log_info*)malloc(sizeof(log_info));
			log->port = clientaddr.sin_port;
			log->addr = clientaddr.sin_addr;
			log->fd = *connfd;
			Pthread_create(&tid, NULL, thread, log);

		}



    exit(0);
}

void* thread(void* vargp)
{
	rio_t rio_client_to_proxy;
	//printf("DEBUG --- Proxy server - entered thread!\n");
	Pthread_detach(pthread_self());

	//P(&mutex_th);

	log_info log = *((log_info*)vargp);
	int connfd = log.fd;
	//char* log_port = (char*)Malloc(sizeof(char)*16);
	char log_port[16];
	sprintf(log_port, "%d", log.port);
	char* log_addr = inet_ntoa(log.addr);
	//printf("DEBUG --- Receiving vargp finished!\n");

	Rio_readinitb(&rio_client_to_proxy, connfd);

	Free(vargp);

	char buf[MAXLINE];
	int read_byte = -1;
	read_byte = Rio_readlineb(&rio_client_to_proxy, buf, MAXLINE);
	//printf("DEBUG --- Proxy server - detached thread and got connfd!\n");
	//Rio_readn_w(connfd, buf, MAXLINE);
	//Rio_readlineb(&rio_client_to_proxy, buf, MAXLINE);
	//printf("DEBUG --- Proxy server - buf : \"%s\"(%d bytes)\n", buf, read_byte);
	while (errno != EPIPE && (read_byte > 0))
	{
		//Rio_readlineb(&rio_client_to_proxy, buf, MAXLINE);
		//printf("DEBUG --- Proxy server - buf : \"%s\"\n", buf);

		//printf("DEBUG --- errno == EPIPE : %d\n", (errno == EPIPE));
		//printf("DEBUG --- Proxy server - read message!\n");
	
		if (strcmp(buf, "\n") == 0)
		{
			Rio_writen_w(connfd, "usage: <host> <port> <message>\n", strlen("usage: <host> <port> <message>\n"));
		}
		else if (host_checker(buf) < 0)
		{
			Rio_writen_w(connfd, "usage: <host> <port> <message>\n", strlen("usage: <host> <port> <message>\n"));
		}
		else if (port_checker(buf) < 0)
		{
			Rio_writen_w(connfd, "usage: <host> <port> <message>\n", strlen("usage: <host> <port> <message>\n"));
		}
		else
		{
			//P(&mutex_oc);
	
			char* host; int port_server; char* msg; 
			rio_t rio;
			
			//char* buf_for_host = (char*)Malloc(sizeof(char)*MAXLINE); 
			char buf_for_host[MAXLINE];
			strcpy(buf_for_host, buf);
			//char* buf_for_port_server = (char*)Malloc(sizeof(char)*MAXLINE); 
			char buf_for_port_server[MAXLINE];
			strcpy(buf_for_port_server, buf);
			//char* buf_for_msg = (char*)Malloc(sizeof(char)*MAXLINE); 
			char buf_for_msg[MAXLINE];
			strcpy(buf_for_msg, buf);
			host = get_host(buf_for_host); 
			port_server = atoi(get_port(buf_for_port_server));
			msg = get_message(buf_for_msg);
			//char* msg_received = (char*)Malloc(sizeof(char) * strlen(msg));
			char msg_received[MAXLINE];
			//printf("DEBUG --- Proxy server - host : \"%s\"\n", host);
			//printf("DEBUG --- Proxy server - port_server : \"%d\"\n", port_server);
			//printf("DEBUG --- Proxy server - msg : \"%s\"\n", msg);
			/*if (host == NULL || port_server == 0 || msg == NULL || (host_checker(host) < 0) || (port_checker(port_server) < 0))
			{
				Rio_writen_w(connfd, "usage: <host> <port> <message>\n", strlen("usage: <host> <port> <message>\n"));
			}*/
			//else
			//{
				P(&mutex_log);
				int proxy_fd = open_clientfd_ts(host, port_server, &mutex_oc);
				Rio_readinitb(&rio, proxy_fd);
				
				Rio_writen_w(proxy_fd, msg, strlen(msg));
				Rio_readlineb_w(&rio, msg_received, MAXLINE);
				Close(proxy_fd);
				//printf("DEBUG --- Got message from server!\n");			
				//P(&mutex_log);
				char received_len[16];// = (char*)Malloc(sizeof(char)*16);
				sprintf(received_len, "%d", (int)strlen(msg_received));
				//printf("DEBUG --- Generated date!\n");
				FILE *log_file;
				log_file = fopen(PROXY_LOG, "a");
				time_t current_time = time(NULL);
				struct tm tm = *localtime(&current_time);
				char time_str[MAXLINE];
				//char* time_str = (char*)Malloc(sizeof(char)*MAXLINE);
				//printf("DEBUG --- Getting time done!\n");
				//setlocale(LC_TIME, "ko_KR.utf8");
				strftime(time_str, (size_t)128, "%a %d %b %Y %H:%M:%S %Z", &tm);
				strftime(time_str, (size_t)128, "%A %c", &tm);
				strcat(time_str, ": ");
				strcat(time_str, log_addr);
				//printf("DEBUG --- log_addr added!\n");
				strcat(time_str, " ");
				strcat(time_str, log_port);
				//printf("DEBUG --- log_port added!\n");
				strcat(time_str, " ");
				strcat(time_str, received_len);
				//printf("DEBUG --- received_len added!\n");
				strcat(time_str, " ");
				strcat(time_str, msg_received);
				//printf("DEBUG --- msg_received added!\n");
				//strcat(time_str, "\n");
				//strftime(time_str, (size_t)128, "%A %c", &tm);
				fputs(time_str, log_file);
				fclose(log_file);
				//V(&mutex_log);

				Rio_writen_w(connfd, msg_received, strlen(msg_received));
			//}

			//V(&mutex_oc);
				//close(proxy_fd);
				V(&mutex_log);
		}
		
		read_byte = Rio_readlineb(&rio_client_to_proxy, buf, MAXLINE);
	}

	Close(connfd);
	//V(&mutex_th);
	//Pthread_exit(NULL);
	return NULL;
}



int open_clientfd_ts(char* hostname, int port, sem_t* mutexp)
{
	int clientfd;
	int len = 16;
	int ghn_res = -1;
	int err;
	struct hostent* hp;
	struct hostent hbuf;
	struct sockaddr_in serveraddr;
	P(mutexp);
	char* buf = (char*)Malloc(sizeof(char) * len);
	if ((clientfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		return -1;
	}

	while ((ghn_res = gethostbyname_r(hostname, &hbuf, buf, len, &hp, &err)) == ERANGE)
	{
		len = len * 2;
		buf = realloc(buf, len);
		if (NULL == buf)
		{
			perror("realloc");
		}
	}

	if (hp == NULL)
	{
		return -2;
	}

	bzero((char*)&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	bcopy((char*)hp->h_addr_list[0], (char*)&serveraddr.sin_addr.s_addr, hp->h_length);
	serveraddr.sin_port = htons(port);

	if (connect(clientfd, (SA*)&serveraddr, sizeof(serveraddr)) < 0)
	{
		return -1;
	}
	V(mutexp);
	return clientfd;
}

ssize_t Rio_readn_w(int fd, void* ptr, size_t nbytes)
{
	ssize_t n;
	
	if ((n = rio_readn(fd, ptr, nbytes)) < 0)
	{
		fprintf(stderr, "%s: %s\n", "Rio_readn_w error", strerror(errno));
		return 0;
	}
	return n;
}

ssize_t Rio_readlineb_w(rio_t* rp, void* usrbuf, size_t maxlen)
{
	ssize_t rc;
	if ((rc = rio_readlineb(rp, usrbuf, maxlen)) < 0)
	{
		fprintf(stderr, "%s: %s\n", "Rio_readlineb error", strerror(errno));
		return 0;
	}
	return rc;
}

void Rio_writen_w (int fd, void* usrbuf, size_t n)
{
	if (rio_writen(fd, usrbuf, n) != n)
	{
		fprintf(stderr, "%s: %s\n", "Rio_writen error", strerror(errno));
	}
	return;
}


