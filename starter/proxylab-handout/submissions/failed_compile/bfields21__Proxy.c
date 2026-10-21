#include <stdio.h>
#include "csapp.h"
#include "cache.h"
#include "cstring.h"
// prototypes for functions defined and used in this file
static void handleRequest(int connfd);
static void getRequest(int connfd, char *method, char *uri,
                       char *version, char *host);
static int isValid(char *method, char *version);
static void parseURI(char *uri, char *host, char *path, char *port);
static void getPort(char *port, char *uri);
static void getPath(char *path, char *uri);
static void getHost(char *host, char *uri);
static void makeRequest(int connfd, char *uri, char *request,
                        char *host, char *port);
static void buildRequest(char *request, char *host,
                         char *path, char *version);

//
// Create a listening socket. Accept connection requests on
// the listening socket.  Read one request from the connected
// socket, respond to it, and close the socket.
//
// Helpful code: main in tiny.c
//
int main(int argc, char **argv)
{
   // TODO
   int listenfd, connfd; // uniq nums
   char hostname[MAXLINE], port[MAXLINE];
   socklen_t clientlen;
   struct sockaddr_storage clientaddr; // meta data ip port w/e

   /* Check command line args */
   // If there is less or more than 2 arguments then exit with error message
   if (argc != 2)
   {
      fprintf(stderr, "usage: %s <port>\n", argv[0]);
      exit(1);
   }

   // Call Open_listenfd on the port passed in.
   listenfd = Open_listenfd(argv[1]); // argv 1 is the port
   while (1)
   {
      clientlen = sizeof(clientaddr);                              // set the the clientaddr
      connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);    // listenfd is to listen to that one file
      Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, // collect meta data
                  port, MAXLINE, 0);
      printf("Accepted connection from (%s, %s)\n", hostname, port);
      handleRequest(connfd); // will call handelRequest
      Close(connfd);         // line to close the file
   }
   return 1;
}

// handleRequest
// Reads the request from the connected socket by calling getRequest.
// If the method or version isn't valid, the function returns
// without doing anything.
//
// Otherwise, it checks to see if the item is cached.
// If it is, the item is written to the connected socket.
// If it is not, it parses the uri to get the host, the path
// to the file, and the port. Then, it builds the
// request. Finally, it sends the request, gets the response,
// sends the response to the connected socket, and potentially
// caches the response.
//
// Helper functions: getRequest, isValid, findCacheItem,
//                   Rio_writen (for writing object from cache),
//                   parseURI, buildRequest, makeRequest
void handleRequest(int connfd)
{
   char method[MAXLINE];  // first word on request line, such as GET
   char uri[MAXLINE];     // uri on the request line
   char version[MAXLINE]; // version on the request line, such as HTTP/1.1
   char port[MAXLINE];    // port obtained from the request line or 80
   char host[MAXLINE];    // host from the HOST: header or from the uri if no header
   char path[MAXLINE];    // path to table obtained from the request line
   char request[MAXBUF];  // request to send to origin server

   // TODO
   // 1) call getRequest to read the request from the client

   ///(int connfd, char * method, char * uri,
   ///             char * version, char * host)
   getRequest(connfd, method, uri, version, host);
   // 2) check to see if the request is valid by calling isValid

   //(char * method, char * version)

   int valid = isValid(method, version);
   // 3) if the request is valid then
   //    a) parse URI by calling parsing URI
   //    b) build the request to send to the origin server by calling buildRequest
   //    c) send request to the origin server and send response back to client by calling makeRequest
   if (valid)
   {
      printf("SUCCESS!!!\n");
      // parseURI(char * uri, char * host, char * path, char * port)
      parseURI(uri, host, path, port);
      printf("(%s, %s, %s)\n", host, path, port);
      // buildRequest(char * request, char * host, char * path, char * version)
      buildRequest(request, host, path, version);
      printf(request);
      // makeRequest(int connfd, char * uri, char * request, char * host, char * port)
      makeRequest(connfd, uri, request, host, port);
   }

   return;
}

// buildRequest
// Build the request to send to the origin server.
// The request needs to have the:
//    1) request line (GET path version)
//    2) Host: header line
//    3) User-Agent: header line
//    4) Connection: close
//    5) Proxy-Connection: close
//
// Helpful code: serve_static in tiny.c (note its use of sprintf)
void buildRequest(char *request, char *host, char *path, char *version)
{
   char *userAgentHdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";
   // TODO
   // use sprintfs to write to request array
   sprintf(request, "GET %s %s\r\n", path, version);
   sprintf(request, "%sHost: %s\r\n", request, host);
   sprintf(request, "%sUser-Agent: %s\r\n", request, userAgentHdr);
   sprintf(request, "%sConnection: close\r\n", request);
   sprintf(request, "%sProxy-Connection: close\r\n\r\n", request);

   return;
}

// makeRequest
// This function connects to the origin server.
// If the open succeeds, the request is written to the socket.
// After that, the response from the origin server is read in
// a loop and sent to the client via the connected socket.
//
// In addition, space (MAX_OBJECT_SIZE bytes) is dynamically allocated
// to hold the object using the function Calloc.
// The response from the  origin server is copied (piece by piece) into the dynamically
// allocated space using a memcpy.  (memcpy needs to be used instead of
// strcpy because the object might be binary.) If the object isn't too big,
// it is added to the cache.  If it is too big, the dynamically
// allocated space is freed.
//
// Helper functions: Calloc, Open_clientfd, Rio_writen, Rio_readinitb,
//                   Rio_readlineb, addCacheLine, Close, various C functions
//
// Helpful code: Nothing complete fits, but you could look at
//               serve_static and read_requesthdrs in tiny.c
void makeRequest(int connfd, char *uri, char *request,
                 char *host, char *port)
{
   // TODO
   // Call findCacheItem to see object is cached and if it is
   // then use Rio_writen to write to the connected socket (connfd)
   //(Initially, write the code without using caching. Incremental development!)
   //
   // Otherwise,
   //    a) open connection with origin server
   //    b) write request to origin server
   //    c) allocate space using Calloc to hold response
   //    d) read the response in a loop using Rio_readlineb until there is no
   //       more to be read and copy response to the dynamically allocated space.
   //       Also, write the response to the connected socket.
   //
   // If caching, then call addCacheItem if the size of the object is
   // smaller than the MAX_OBJECT_SIZE; otherwise, delete the dynamically
   // allocated object.
   //
   // Close the connected socket

   char buf[MAXLINE];
   rio_t rio;
   size_t lineSize;
   int objectSize = 0;
   char *cacheItem = (char *)Calloc(1, MAX_OBJECT_SIZE);

   if (findCacheItem(cacheItem))
   {
      printf("Item already cache!");
   }
   else
   {
      int newConnfd = Open_clientfd(host, port);
      Rio_readinitb(&rio, newConnfd);
      Rio_writen(newConnfd, request, MAXLINE);
      lineSize = Rio_readlineb(&rio, buf, MAXLINE);

      while (lineSize)
      {
         Rio_writen(connfd, buf, lineSize);
         objectSize = objectSize + lineSize;

         if (objectSize <= MAX_OBJECT_SIZE)
         {
            memcpy(cacheItem + objectSize - lineSize, buf, lineSize);
         }

         lineSize = Rio_readlineb(&rio, buf, MAXLINE);
      }

      if (objectSize <= MAX_OBJECT_SIZE)
      {
         addCacheItem(uri, cacheItem, objectSize);
      }
      else
      {
         // delete the dynamically
         // allocated object.
         free(cacheItem);
      }

      Close(newConnfd);
   }

   return;
}

// getPort
// Retrieves the port from the uri if present.
// If not present, the port defaults to "80"
// Example:  http://localhost:12345/godzilla.jpg
//          port is 12345
void getPort(char *port, char *uri)
{
   // TODO
   // note port is between the second ':' and the third '/'
   // If no second colon then set port to "80"
   int getIndexPortStart = indexOf(uri, ':', 2);
   int getIndexPortEnd = indexOf(uri, '/', 3);

   if (getIndexPortStart == -1)
   {
      // port = "80";
      strncpy(port, "80", 2);
   }
   else
   {
      buildCString(port, uri, getIndexPortStart + 1, getIndexPortEnd - 1);
   }

   // printf("this is the port number: %s\n", port);

   return;
}

// getHost
// Retrieves the host from the uri.  This function is
// called if the request doesn't have a Host line.
// Example:  http://localhost:12345/godzilla.jpg
//           host is localhost
void getHost(char *host, char *uri)
{
   // TODO
   // note host is between the second '/' and the second ':'
   // If there isn't a second ':' the host is between the
   // second '/' and the third '/'
   int getIndexHostStart = indexOf(uri, '/', 2);
   int getIndexHostEnd = indexOf(uri, ':', 2);

   if (getIndexHostEnd == -1)
   {
      int getIndexHostEnd = indexOf(uri, '/', 3);
   }
   buildCString(host, uri, getIndexHostStart + 1, getIndexHostEnd - 1);

   // printf("this is the Host number: %s\n", host);

   return;
}

// getPath
// Retrieves the path from the uri.
// Example:  http://localhost:12345/godzilla.jpg
//           path is /godzilla.jpg
void getPath(char *path, char *uri)
{
   // TODO
   // note path starts at the third '/' and goes to the end
   // of the line (use strlen)
   int stringlength = strlen(uri);

   int getIndexPathStart = indexOf(uri, '/', 3);

   buildCString(path, uri, getIndexPathStart, stringlength - 1);

   // printf("this is the path number: %s\n", path);

   return;
}

// parseURI
// Parses the uri to obtain the host, path, and port from
// the uri. host may have been already obtained from a
// Host: header liner.
//
// Helper functions: getHost, getPort, getPath
void parseURI(char *uri, char *host, char *path, char *port)
{
   // TODO
   // call getHost if host is strlen(host) is 0
   if (strlen(host) == 0)
   {
      getHost(host, uri);
   }
   // printf("this is the host in parseURI: %s\n", host);
   getPath(path, uri);
   getPort(port, uri);
   // call getPath to get the path
   // call getPort to get the port
   // printf("Theses are the host path and port: (%s,%s,%s)", host, path, port);
   return;
}

// getRequest
// Reads the request from the client via the connected
// socket. Gets the method, uri, and version from the
// request line.  The headers are read and discarded except
// for the Host: header line.  In that case, the host
// buffer is initialized to the host from the Host:
// header line.  For example,
// Host: localhost:12345
// causes host to be set to "localhost"
//
// Helper functions: Rio_readinitb, Rio_readlineb,
//                   various C functions
// Helpful code: read_requesthdrs in tiny.c
void getRequest(int connfd, char *method, char *uri,
                char *version, char *host)
{

   // TODO
   // First, read the request line (see read_requesthdrs in tiny.c)
   rio_t rio;
   char buf[MAXLINE];

   Rio_readinitb(&rio, connfd);
   Rio_readlineb(&rio, buf, MAXLINE);
   // printf("This is the requestLine PXBF: %s", buf);
   //  Then use your cstring functions to grab the method, uri, and
   //  version from the request line
   //  request line can look like: GET http://student2.cs.appstate.edu/~can HTTP/1.1
   //  Note that a single space is between each of the three fields

   int getIndexMethod = indexOf(buf, ' ', 1);
   int getIndexUri = indexOf(buf, ' ', 2);
   int getIndexVersion = indexOf(buf, '\r', 1);

   buildCString(method, buf, 0, getIndexMethod - 1);
   buildCString(uri, buf, getIndexMethod + 1, getIndexUri - 1);
   buildCString(version, buf, getIndexUri + 1, getIndexVersion - 1);
   // printf("this is the method string: %s\n", method);
   // printf("this is the uri string: %s\n", uri);
   // printf("this is the version string: %s\n", version);

   Rio_readlineb(&rio, buf, MAXLINE);
   while (strcmp(buf, "\r\n"))
   {
      Rio_readlineb(&rio, buf, MAXLINE);
      // printf("This is the WHOLE host Line: %s", buf);
      if (!strncmp(buf, "Host:", 4))
      {
         int i = 6;
         while (buf[i] != ':' && buf[i] != '\r')
         {
            host[i - 6] = buf[i];
            i++;
         }
         host[i - 6] = '\0';
      }
   }
   // printf("this is the host string: %s", host);
   return;

   //
   // Next, use a loop to read the remaining headers of the request until a single
   // line with "\r\n" has been read.
   // In the loop body, look for "Host:" If present then grab value for host from
   // that line.
   // host header line could look like:
   // Host: localhost:8080
   // or
   // Host: student2.cs.appstate.edu
   // The first would cause host to be set to "localhost".
   // The second would cause host to be set to "student2.cs.appstate.edu"
}

// isValid
// This function returns 1 if the method is GET
// and the version is either HTTP/1.1 or HTTP/1.0.
// Otherwise this function returns 0.
int isValid(char *method, char *version)
{
   // TODO
   if (!strcmp(method, "GET") && (!strcmp(version, "HTTP/1.1") || !strcmp(version, "HTTP/1.0")))
   {
      return 1;
   }
   return 0;
}
