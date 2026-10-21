
/*
 * proxy.c - A Simple Sequential Web proxy
 *
 * Course Name: 14:332:456-Network Centric Programming
 * Assignment 2
 * Student Name: Ashwin Anand
 * 
 * IMPORTANT: Give a high level description of your code here. You
 * must also provide a header comment at the beginning of each
 * function that describes what that function does.
 */ 


// packages
#include "csapp.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/*
 * Function prototypes
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, char *uri, int size);

// main method
int main(int argc, char **argv)
{
	/* Check arguments */
	if (argc != 2) 
	{
		fprintf(stderr, "Usage: %s <port number>\n", argv[0]);
		exit(0);
	}

	// web brower file directory
	int web_browser_file_directory;

	// int_user_port contains user command line input converted to int through atoi function
	int user_input_port = atoi(argv[1]); 

	// listen_file_directory will make the socket and listen and bind it to the user_input_port
	int listen_file_directory = Open_listenfd(user_input_port);

	// client_address created 
	struct sockaddr_in client_address;

	// buffer that will be as big as MAXLINE
	char buffer[MAXLINE];

	// request type
	char type_of_request[10];

	// request
	char request_web[MAXLINE];

	// URL that will be as big as MAXLINE
	char URL[MAXLINE];

	// host that will be as big as MAXLINE
	char host[MAXLINE];

	// it will contain string of host with maxline capacity
	char string_of_host[MAXLINE];

	// while loop to keep on listening to all requests
	while(1) 
	{		
		// client_address_length will contain the size of clineta
	    int client_address_length = sizeof(client_address);
	
	    // Accepting any request that are coming to it 
	    int web_browser_file_directory = Accept(listen_file_directory,(SA *)&client_address, &client_address_length);

		// acception error thrown out of less than 0
		if (web_browser_file_directory < 0)
		{
			perror("Accepting connections error");
		}

		// reio_object_1 creation
		rio_t rio_object_1;

		// reio_object_2 creation
		rio_t rio_object_2;
	
		// reads in socket to rio_object_1
		Rio_readinitb(&rio_object_1,web_browser_file_directory);

	    // Let's read a request from the browser into the buffer
	    ssize_t browser_read_to_buffer = Rio_readlineb(&rio_object_1,buffer,MAXLINE);
		
		// Get the client IP using the clientaddr struct
		char *Client_IP_Address = inet_ntoa(client_address.sin_addr);
	
		// making a colon counter variable to hold buffer and act as pointer
		char *colon_buffer = buffer;

		// color counter to increment
		int colon_counter= 0;

		// client port 
		int client_port_converted = 0;

		// while loop to go through and find any 0
		while((*colon_buffer != '\0') && (colon_counter != 2))
		{
			// buffer that will go into increment colon_counter 
			if(*colon_buffer == ':')
			{	
				colon_counter++;
			}

			// colon_buffer incremented 
			colon_buffer++;
		}
		
		// isdigit checks whether colorbuffer+1 is a numerical integer value between 0 to 9
		if(isdigit(*(colon_buffer+1))) 
		{
			// temporary_port to hold colon buffer
			char temporary_port[10];

			// a acts as counter variable
			int a = 0;

			// while loop that keep on going unitl it doesn't hit reach a /
			while(*colon_buffer != '/') 
			{
				// temporary port will contain a colon_buffer char as it runs through the loop
				temporary_port[a] = *colon_buffer;

				// incrementing it 
				colon_buffer++;

				// incrementing it
				a++;
			}

			// client port that has been changed to int value through atoi function
			client_port_converted = atoi(temporary_port);
		}
		else 
		{
			// scans the buffer, requesttype and url and gets the url
			sscanf(buffer,"%s %s HTTP/1.1\r\n",type_of_request,URL); 

			// codes the int client port to be 80
			client_port_converted = 80;
		}

		// browser read to buffer will read the string of host
		browser_read_to_buffer = Rio_readlineb(&rio_object_1,string_of_host,MAXLINE);

		// scans the string of host for the host
		sscanf(string_of_host,"Host: %s\r\n",host); 

		// open clientfd that takes in  host and flinet port
	    int server_file_directory = Open_clientfd(host,client_port_converted);

		// it will read in it to the buffer from string of host
	    Rio_readinitb(&rio_object_2,server_file_directory);
	
		//  This makes the request of web to the web 
		sprintf(request_web,"%s %s HTTP/1.0\r\nHost: %s\r\n\r\n",type_of_request,URL,host);	

		// writes the request from reques to the string of host
	    Rio_writen(server_file_directory,request_web,strlen(request_web));

		//the message log that will be sent to the file to be written in
		char message_for_log[40 + strlen(Client_IP_Address) + strlen(URL)];

		// the content length will be 15 max
		char length_of_content[15];

		// pointer of content that will be referred to later
		char *pointer_of_content;

		// opening log file 
		FILE *file_log_info = fopen("PLog.txt","a");

		// web_response to size of MAXLINE
		char web_response[MAXLINE];

		// Retrieve response from the server and forward to browser
		while((browser_read_to_buffer = Rio_readlineb(&rio_object_2,web_response,MAXLINE)) > 0)
		 {
			// if loop to make sure that there is content in the pointer 
			if((pointer_of_content = strstr(web_response,"Content-Length: ")) != NULL)
			{
				sscanf(web_response,"Content-Length: %s\r\n",length_of_content);
				format_log_entry(message_for_log,&client_address,URL,atoi(length_of_content)); 
				fprintf(file_log_info,"%s\n",message_for_log); 
			}
			
			
			// writing into the proxy log text file	
			Rio_writen(web_browser_file_directory,web_response,browser_read_to_buffer);
		}
		// letting user know when info is logged
		//printf("Sent to log text file\n");

		// closing file_log_info
		fclose(file_log_info);
		
	}

	// close the web brower file directory and string of host
	close(web_browser_file_directory);
	close(string_of_host);	

	// exiting
	exit(0);
}

/*
 * format_log_entry - Create a formatted log entry in logstring. 
 * 
 * The inputs are the socket address of the requesting client
 * (sockaddr), the URI from the request (uri), and the size in bytes
 * of the response from the server (size).
 */
void format_log_entry(char *logstring, struct sockaddr_in *sockaddr, 
		      char *uri, int size)
{
    time_t now;
    char time_str[MAXLINE];
    unsigned long host;
    unsigned char a, b, c, d;

    /* Get a formatted time string */
    now = time(NULL);
    strftime(time_str, MAXLINE, "%a %d %b %Y %H:%M:%S %Z", localtime(&now));

    /* 
     * Convert the IP address in network byte order to dotted decimal
     * form. Note that we could have used inet_ntoa, but chose not to
     * because inet_ntoa is a Class 3 thread unsafe function that
     * returns a pointer to a static variable (Ch 13, CS:APP).
     */
    host = ntohl(sockaddr->sin_addr.s_addr);
    a = host >> 24;
    b = (host >> 16) & 0xff;
    c = (host >> 8) & 0xff;
    d = host & 0xff;


    /* Return the formatted log entry string */
	// I have added size to the sprintf to be printed
    sprintf(logstring, "%s: %d.%d.%d.%d %s %d", time_str, a, b, c, d, uri, size);
}