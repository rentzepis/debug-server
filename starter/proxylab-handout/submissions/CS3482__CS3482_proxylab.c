#include <stdio.h>
#include "csapp.h"  
#include "cache.h"

//prototypes for functions defined and used in this file
static void handleRequest(int connfd);
static void getRequest(int connfd, char * method, char * uri, 
                       char * version, char * host);
static int isValid(char * method, char * version);
static void parseURI(char * uri, char * host, char * path, char * port);
static void getPort(char * port, char * uri);
static void getPath(char * path, char * uri);
static void getHost(char * host, char * uri);
static void makeRequest(int connfd, char * uri, char * request, 
                        char * host, char * port);
static void buildRequest(char * request, char * host,
                         char * path, char * version);

//
// Create a listening socket. Accept connection requests on
// the listening socket.  Read one request from the connected
// socket, respond to it, and close the socket.
//
// Helpful code: main in tiny.c
//
int main(int argc, char **argv)
{

   //if extra args or too few args 
   if (argc != 2)
   {
      fprintf(stderr, "usage: %s <port>\n", argv[0]);
      exit(1);
   }
   cacheInit(); //initialize cache to caching web objects
   
   //TODO
   //
   //This is the part of the proxy where it is acting as a server!
   //
   //Create a listening socket by calling Open_listenfd and
   //then loop forever.
   //In the loop:
   // 1) accept the connection request
   // 2) printed a message about the client: (hostname, port)
   // 3) call handleRequest to handle the request
   // 4) close the connection
   //See main in tiny.c
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
   char method[MAXLINE];  //first word on request line, such as GET
   char uri[MAXLINE];     //uri on the request line
   char version[MAXLINE]; //version on the request line, such as HTTP/1.1
   char port[MAXLINE];    //port obtained from the request line or 80 
   char host[MAXLINE];    //host from the HOST: header or from the uri if no header
   char path[MAXLINE];    //path to table obtained from the request line
   char request[MAXBUF];  //request to send to origin server

   //TODO
   //1) call getRequest to read the request from the client
   //   method, uri, version, and potentially, host and port arrays
   //   are filled by call
   //
   //2) check to see if the request is valid by calling isValid
   //    
   //3) if the request is valid then 
   //   a) parse URI by calling parseURI (fills host, path, port arrays)
   //   b) build the request to send to the origin server by calling buildRequest
   //      (fills request array)
   //   c) send request to the origin server and send response back to client by 
   //      calling makeRequest

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
void buildRequest(char * request, char * host, char * path, char * version)
{
   char *userAgentHdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3";
   //TODO
   //use sprintfs to write to request array (see serve_static)
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
void makeRequest(int connfd, char * uri, char * request, 
                 char * host, char * port)
{
   //TODO
   //Call findCacheItem to see object is cached and if it is
   //then use Rio_writen to write it to the connected socket (connfd)
   //(Initially, write the code without using caching. Incremental development!)
   //
   //Otherwise, 
   //   This is where the proxy acts like a client!
   //   a) open connection with origin server
   //   b) write request to origin server
   //   c) allocate space using Calloc to hold response (don't do this until
   //      you add the caching code)
   //   d) read the response in a loop using Rio_readlineb until there is no
   //      more to be read and write the response to the connected socket.
   //      Also, for caching, copy response to the appropriate spot in the
   //      dynamically allocated space.
   //      Note: Rio_readlineb returns the number of bytes that are read.
   //      Note: the third parameter to Rio_writen is the number of bytes 
   //            to write to socket
   //
   //If caching, then call addCacheItem if the size of the object is
   //smaller than the MAX_OBJECT_SIZE; otherwise, delete the dynamically
   //allocated object. 
   //
   //Close the connected socket
   return;
}

// getPort
// Retrieves the port from the uri if present.
// If not present, the port defaults to "80"
// Example:  http://localhost:12345/godzilla.jpg
//          port is 12345
void getPort(char * port, char * uri)
{
   //TODO
   //note port is between the second ':' and the third '/'
   //If no second colon then set port to "80"
   //Use your indexOf and buildCString functions
   return;
}

// getHost
// Retrieves the host from the uri.  This function is
// called if the request doesn't have a Host line.
// Example:  http://localhost:12345/godzilla.jpg
//           host is localhost
void getHost(char * host, char * uri)
{
   //TODO
   //note host is between the second '/' and the second ':'
   //If there isn't a second ':' the host is between the
   //second '/' and the third '/'
   //Use your indexOf and buildCString functions
   return;
}

// getPath
// Retrieves the path from the uri.
// Example:  http://localhost:12345/godzilla.jpg
//           path is /godzilla.jpg
void getPath(char * path, char * uri)
{
   //TODO
   //note path starts at the third '/' and goes to the end
   //of the line (use indexOf and strlen)
   return;
}

// parseURI
// Parses the uri to obtain the host, path, and port from
// the uri. host may have been already obtained from a
// Host: header liner.
//
// Helper functions: getHost, getPort, getPath
void parseURI(char * uri, char * host, char * path, char * port)
{
   //TODO
   //call getHost if host is strlen(host) is 0
   //call getPath to get the path
   //call getPort to get the port
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
void getRequest(int connfd, char * method, char * uri, 
                char * version, char * host)
{

   //TODO
   //First, read the request line (see read_requesthdrs in tiny.c)
   //Then use your cstring functions to grab the method, uri, and
   //version from the request line
   //request line can look like: GET http://student2.cs.appstate.edu/~can HTTP/1.1
   //Note that a single space is between each of the three fields
   //
   //Next, use a loop to read the remaining headers of the request until a single
   //line with "\r\n" has been read.
   //In the loop body, look for "Host:" If present then grab value for host from
   //that line.
   //host header line could look like: 
   //Host: localhost:8080
   //or
   //Host: student2.cs.appstate.edu
   //The first would cause host to be set to "localhost".
   //The second would cause host to be set to "student2.cs.appstate.edu"
   //Ignore the port if there is one.
   //
   return;
}

// isValid
// This function returns 1 if the method is GET
// and the version is either HTTP/1.1 or HTTP/1.0.
// Otherwise this function returns 0.
int isValid(char * method, char * version)
{
   //TODO
   //use strcmp to check for the valid method and versions
   return 0;
}
   
