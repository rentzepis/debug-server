/*------------------------------------------------------------------------------
 * M1522.000800              System Programming                       Fall 2017
 *
 *                             HTTP & Proxy Lab
 *
 * Author: Barend Harris <barend@csap.snu.ac.kr>
 * Change Log:
 * 2017/11/23 Barend Harris created based on csapp book
 *
 * Copyright (c) 2012-2017, Computer Systems and Platforms Laboratory, SNU
 * All rights reserved.
 *
 *  Redistribution and use in source and binary forms,  with or without modifi-
 *  cation, are permitted provided that the following conditions are met:
 *
 *  - Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,  BUT NOT LIMITED TO,  THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY  AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER  OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT,  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSE-
 *  QUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF  SUBSTITUTE
 *  GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 *  HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT
 *  LIABILITY, OR TORT  (INCLUDING NEGLIGENCE OR OTHERWISE)  ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *----------------------------------------------------------------------------*/
/*
 * proxy.c - A simple, iterative HTTP proxy server
 */
#include "csapp.h"

void doit(int fd);
void print_requesthdrs(rio_t *rp);

//-----------------------------------------------------------------------------
int main(int argc, char **argv)
{
/*
 * main:
 *  listens for connections on the given port number, handles HTTP requests
 *  with the doit function then closes the connection
 */
  
  int listenfd, connfd, port, clientlen;
  struct sockaddr_in clientaddr;
  
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  port = atoi(argv[1]);
  // listen for connections
  listenfd = Open_listenfd(port); 
  // if a client connects, accept the connection, handle the requests
  while(1)
  {
  // (call the do it function), then close the connection
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    doit(connfd);
    Close(connfd);
  }
}

//-----------------------------------------------------------------------------
void doit(int fd)
{
/*
 * doit: reads HTTP request from connection socket, forwards the request to the
 *  requested host. Reads the response from the host server, and writes the
 *  response back to the client
 * params:
 *    - fd (int): file descriptor of the connection socket.
 */
  
  char line1[MAXLINE], line2[MAXLINE];
  int serverfd;
  // send to the http server
  char host_httpserv[MAXLINE];
  int port_httpserv;
  rio_t rio;
  memset(line1, 0, strlen(line1));


  Rio_readinitb(&rio, fd);

  //read line 1, format: method uri version
  Rio_readlineb(&rio, line1, MAXLINE);
  // it should be: GET http://localhost:8080/pages/inde.html HTTP/1.1
  Rio_readlineb(&rio, line2, MAXLINE);
  sscanf(line2, "%*s %[^:]:%d", host_httpserv, &port_httpserv);

  // Send the first line of the client request to the server
  // (only the first line is needed), add a break '\r\n' so the server knows
  // the request has ended
  
  char responseBuffer[MAXLINE], response[MAXLINE];
  char contentBuffer[MAXLINE];
  int contentLength;
  rio_t serverResponse;
  memset(responseBuffer, 0, strlen(responseBuffer));
  memset(response, 0, strlen(response));
 
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];  
  sscanf(line1, "%s %s %s", method, uri, version);
  sscanf(uri, "http://%*[^/]%s", uri);	// new uri stores the path
  sprintf(response, "%s %s %s", method, uri, version);
  strcat(response, "\r\n");
  strcat(response, "\r\n");

  // send the content to the http server
  serverfd = Open_clientfd(host_httpserv, port_httpserv);
  Rio_readinitb(&serverResponse, serverfd);
  Rio_writen(serverfd, response, MAXLINE);
  memset(response, 0, strlen(response));
  // Read the response header from the server and build the proxy's response
  // header by repeatedly adding the responseBuffer (server response)
  while(strcmp(responseBuffer, "\r\n"))
  {
    Rio_readlineb(&serverResponse, responseBuffer, MAXLINE);
    //printf("%s\n", responseBuffer);
    //fflush(stdout);
    strcat(response, responseBuffer);
  }

  char temp[MAXLINE];
  // we must parse the Content-Length header from the server response in order
  // to know how much content to read from the server and write to the client
  sscanf(response, "%*[^-]%*s %s\r\n", temp);
  contentLength = atoi(temp);  
  // Using the contentLength read from the server response header,
  // we must allocate and read that many bytes to our buffer
  if(contentLength > MAXLINE){
    //char LcontentBuffer[contentLength];
    char * LcontentBuffer = (char *) malloc(contentLength);
    Rio_readnb(&serverResponse, LcontentBuffer, contentLength);
  // We now write the response heading and the content back to the client
    Rio_writen(fd, response, strlen(response));
    Rio_writen(fd, LcontentBuffer, contentLength);
    free(LcontentBuffer);
    //memset(LcontentBuffer, 0, strlen(LcontentBuffer));
  }
  else{
    Rio_readnb(&serverResponse, contentBuffer, contentLength);
    // We now write the response heading and the content back to the client
    Rio_writen(fd, response, strlen(response));
    Rio_writen(fd, contentBuffer, contentLength);

  }
  Close(serverfd);
  memset(response, 0, strlen(response));
  memset(responseBuffer, 0, strlen(responseBuffer));
  memset(contentBuffer, 0, strlen(contentBuffer));
  // Close the connection to the server
}

//-----------------------------------------------------------------------------
void print_requesthdrs(rio_t *rp)
{
/**** DO NOT MODIFY ****/
/**** WARNING: This will read out everything remaining until a line break ****/
/*
 * print_requesthdrs:
 *        reads out and prints all request lines sent to the server
 * params:
 *    - rp: Rio pointer for reading from file
 *
 */
  char buf[MAXLINE];
  Rio_readlineb(rp, buf, MAXLINE);
  while(strcmp(buf, "\r\n")) {
    printf("%s", buf);
    Rio_readlineb(rp, buf, MAXLINE);
  }
  printf("\n");
  return;
}
