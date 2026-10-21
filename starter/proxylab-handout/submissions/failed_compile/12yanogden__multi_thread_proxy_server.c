#include "sbuf.h"
#include "csapp.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define HTTP_REQUEST_MAX_SIZE 4096
#define HTTP_RESPONSE_MAX_SIZE 102400
#define HOSTNAME_MAX_SIZE 512
#define PORT_MAX_SIZE 7
#define URI_MAX_SIZE 4096
#define VERSION_MAX_SIZE 8
#define HEADERS_MAX_SIZE 4096

#define MAXLINE 8192
#define NTHREADS 100
#define SBUF_SIZE 20

sbuf_t sbuf; // Maybe should make this a pointer?

static char *addHeaders = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\nConnection: close\r\nProxy-Connection: close\r\nAccept: */*\r\n\r\n";

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                     Debug                                                      //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
void print_bytes(unsigned char *bytes, int byteslen) {
	int i, j, byteslen_adjusted;

	if (byteslen % 8) {
		byteslen_adjusted = ((byteslen / 8) + 1) * 8;
	} else {
		byteslen_adjusted = byteslen;
	}
	for (i = 0; i < byteslen_adjusted + 1; i++) {
		if (!(i % 8)) {
			if (i > 0) {
				for (j = i - 8; j < i; j++) {
					if (j >= byteslen_adjusted) {
						printf("  ");
					} else if (j >= byteslen) {
						printf("  ");
					} else if (bytes[j] >= '!' && bytes[j] <= '~') {
						printf(" %c", bytes[j]);
					} else {
						printf(" .");
					}
				}
			}
			if (i < byteslen_adjusted) {
				printf("\n%02X: ", i);
			}
		} else if (!(i % 4)) {
			printf(" ");
		}
		if (i >= byteslen_adjusted) {
			continue;
		} else if (i >= byteslen) {
			printf("   ");
		} else {
			printf("%02X ", bytes[i]);
		}
	}
	printf("\n");
}

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                     Setup                                                      //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
//--------------------------------------- Determine if Request is Complete ---------------------------------------//
int is_complete_request(const char *request) {
	int out = strcmp(&request[strlen(request)-4], "\r\n\r\n") == 0 ? 1 : 0;

	return out;
}

//------------------------------------------------ Assemble Hints ------------------------------------------------//
struct addrinfo assemble_hints() {
	struct addrinfo hints;

	// Allocate memory
	memset(&hints, 0, sizeof(struct addrinfo));

	// Set values
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	return hints;
}

//--------------------------------------------- Get an Address List ----------------------------------------------//
struct addrinfo *getAddressList(char *serverName, char *serverPort) {
	struct addrinfo hints = assemble_hints();
	struct addrinfo *addressList;
	int status = getaddrinfo(serverName, serverPort, &hints, &addressList);

	if (status != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
		exit(EXIT_FAILURE);
	}

	return addressList;
}

//--------------------------------------------- Get a Server Socket ----------------------------------------------//
void getServerSocket(int *serverSocket, char *hostname, char *port) {
	struct addrinfo *addressList = getAddressList(hostname, port);
	struct addrinfo *address;

	// Iterates through addressList: if valid, establish connection
	for (address = addressList; address != NULL; address = address->ai_next) {
		// Establishes local socket
		*serverSocket = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
		
		// If invalid, continue to next address
		if (*serverSocket == -1)
			continue;

		// Establishes connection
		if (connect(*serverSocket, address->ai_addr, address->ai_addrlen) == -1) {
			// If invalid, close socket
			close(*serverSocket);

		} else {
			// If valid, break for
			break;
		}
	}

	// If no valid address, exit
	if (address == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	// Frees addressList
	freeaddrinfo(addressList);
}

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                    Parsers                                                     //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
//------------------------------------------------ Parse Hostname ------------------------------------------------//
void parse_hostname(const char *bufIn, char *hostname) {
	char *delimiter1 = " http://";
	char *delimiter2 = ":";
	char *delimiter3 = "/";
	char *bufHostname = strstr(bufIn, delimiter1) + strlen(delimiter1);
	int hostnameSize;
	
	// Calculate hostnameSize
	if (strstr(bufHostname, delimiter2)) {
		hostnameSize = strstr(bufHostname, delimiter2) - bufHostname;
	} else {
		hostnameSize = strstr(bufHostname, delimiter3) - bufHostname;
	}

	// Assign hostname
	memcpy(hostname, bufHostname, hostnameSize);
	*(hostname + hostnameSize) = '\0';

	printf("hostname: %s\n", hostname);
}

//-------------------------------------------------- Parse Port --------------------------------------------------//
void parse_port(const char *bufIn, char *port) {
	char *delimiter1 = "GET http://";
	char *delimiter2 = ":";
	char *hostname = strstr(bufIn, delimiter1) + strlen(delimiter1);
	
	// If port is explicit
	if (strstr(hostname, delimiter2)) {
		// Calculate portSize
		char *delimiter3 = "/";
		char *bufPort = strstr(hostname, delimiter2) + strlen(delimiter2);
		char *end = strstr(bufPort, delimiter3);
		int portSize = end - bufPort;

		// Assign port
		memcpy(port, bufPort, portSize);
		*(port + portSize) = '\0';
	} else {
		memcpy(port, "80\0", 3);
	}

	printf("port: %s\n", port);
}

//-------------------------------------------------- Parse URI ---------------------------------------------------//
void parse_uri(const char *bufIn, char *uri) {
	char *delimiter1 = " http://";
	char *delimiter2 = "/";
	char *delimiter3 = " ";
	char *bufURI = strstr((strstr(bufIn, delimiter1) + strlen(delimiter1)), delimiter2);
	char *end = strstr(bufURI, delimiter3);
	int uriSize = end - bufURI;

	// Assign uri
	memcpy(uri, bufURI, uriSize);
	*(uri + uriSize) = '\0';

	printf("uri: %s\n", uri);
}
//------------------------------------------------ Parse Headers -------------------------------------------------//
// int count_headers(const char *headers) {
// 	char *scanner = headers;
// 	char *end = strstr(headers, "\r\n\r\n");
// 	int headerCount = 0;
	
// 	while (1) {
// 		const char *headerEnd = strstr(scanner, "\r\n");
		
// 		headerCount++;

// 		if (headerEnd == end) {
// 			break;
// 		} else {
// 			scanner += headerEnd + 2;
// 		}
// 	}

// 	return headerCount;
// }

// char **getAddHeaders() {
// 	char *scanner = addHeaders;
// 	int addHeaderCount = count_headers(addHeaders);
// 	char *addHeaderArray[addHeaderCount];

// 	for (int i = 0; i < addHeaderCount; i++) {
// 		addHeaderArray[i] = scanner;

// 		if (i < addHeaderCount - 1) {
// 			scanner = strstr(scanner, "\r\n") + 2;
// 		}
// 	}

// 	return addHeaderArray;
// }

// int isHeaderUnique(char *header) {
// 	int headerKeySize = strstr(header, ":") - header;
// 	int isHeaderUnique = 0;


// }

// void parse_unique_bufHeaders(const char *bufHeaders, char *uniqueHeaders, int uniqueHeadersSize) {
// 	char *scanner = bufHeaders;
// 	int bufHeadersCount = count_headers(bufHeaders);
// 	char *bufHeadersArray[bufHeadersCount];
// 	char *uniqueBufHeaders[bufHeadersCount];
// 	int j = 0;

// 	for (int i = 0; i < bufHeadersCount; i++) {
// 		bufHeadersArray[i] = scanner;

// 		scanner = strstr(scanner, "\r\n") + 2;
// 	}

// 	for (int i = 0; i < bufHeadersCount; i++) {
// 		if (isHeaderUnique(bufHeadersArray[i])) {
// 			uniqueBufHeaders[j] = bufHeadersArray[i];
			
// 			j++;
// 		}
// 	}
// }

// char *getBufHeaders(const char *bufIn) {
// 	const char *delimiter1 = "HTTP/";
// 	char *bufHeaders = strstr(bufIn, delimiter1) + VERSION_MAX_SIZE + 2;

// 	if (bufHeaders == "\r\n") {
// 		*bufHeaders = '\0';
// 	}

// 	return bufHeaders;
// }

void parse_headers(const char *bufIn, char *headers) {
	// char *uniqueBufHeaders[HEADERS_MAX_SIZE];
	// int uniqueBufHeadersSize = 0;
	int addHeadersSize = (strstr(addHeaders, "\r\n\r\n") + 4) - addHeaders;
	// const char *bufHeaders = getBufHeaders(bufIn);

	// if (bufHeaders) {
	// 	parse_unique_bufHeaders(bufHeaders, uniqueBufHeaders, uniqueBufHeadersSize);
	// }

	memcpy(headers, addHeaders, addHeadersSize);
	*(headers + addHeadersSize) = '\0';

	printf("headers: %s\n", headers);
}

//------------------------------------------ Parse Request from Client -------------------------------------------//
int parse_request(const char *bufIn, char *hostname, char *port, char *uri, char *headers) {
	printf("//----------------------------------- parse_request -----------------------------------//\n");
	printf("\n");
	printf("bufIn:\n");
	// print_bytes(bufIn, (strstr(bufIn, "\r\n\r\n") + 4) - bufIn);
	printf("\n");
	printf("%s\n", bufIn);
	printf("\n");
	fflush(stdout);
	int isComplete = is_complete_request(bufIn);

	if (isComplete) {
		parse_hostname(bufIn, hostname);
		parse_port(bufIn, port);
		parse_uri(bufIn, uri);
		parse_headers(bufIn, headers);
	}

	printf("//-------------------------------------------------------------------------------------//\n");
	printf("\n");
	fflush(stdout);

	return isComplete;
}

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                    Readers                                                     //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
//------------------------------------------- Read Request from Client -------------------------------------------//
void read_request(int *clientSocket, char *bufIn) {
	int readCount = read(*clientSocket, bufIn, HTTP_REQUEST_MAX_SIZE);

	bufIn[readCount] = '\0';

	while(!is_complete_request(bufIn)) {
		readCount += read(*clientSocket, &bufIn[readCount], HTTP_RESPONSE_MAX_SIZE - readCount);
		bufIn[readCount] = '\0';
	}
}

//------------------------------------------ Read Response from Server -------------------------------------------//
void read_server_response(char *response, int *serverSocket, int *responseSize) {
	printf("//------------------------------- read_server_response --------------------------------//\n");
	fflush(stdout);
	int readCount = 0;

	while(1) {
		readCount = read(*serverSocket, &response[*responseSize], HTTP_RESPONSE_MAX_SIZE - *responseSize);

		if (readCount > 0) {
			*responseSize += readCount;
		} else {
			break;
		}
	}

	printf("Response\n");
	// print_bytes(response, readCount);
	printf("\n");
	printf("%s\n", response);
	printf("\n");

	printf("//-------------------------------------------------------------------------------------//\n");
	printf("\n");
	fflush(stdout);
}

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                   Assembler                                                    //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
//------------------------------------------ Assemble Request to Server ------------------------------------------//
void assemble_request(char *request, char *hostname, char *port, char *uri, char *headers) {
	printf("//--------------------------------- assemble_request ----------------------------------//\n");
	fflush(stdout);

	sprintf(request, "GET %s HTTP/1.0\r\nHost: %s\r\n%s", uri, hostname, headers);

	printf("Request\n");
	// print_bytes(request, requestSize);
	printf("\n");
	printf("%s\n", request);
	printf("\n");
	printf("//-------------------------------------------------------------------------------------//\n");
	printf("\n");
	fflush(stdout);
}

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                    Senders                                                     //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
//-------------------------------------------- Send Request to Server --------------------------------------------//
void send_to_server(char *request, int *serverSocket) {
	printf("//---------------------------------- send_to_server -----------------------------------//\n");
	char *pEnd = strstr(request, "\r\n\r\n") + 4;
	int requestLength = pEnd - request;
	int sentCount = 0;
	int sumSent = 0;

	printf("Request\n");
	// print_bytes(request, requestLength);
	printf("\n");
	printf("%s\n", request);
	printf("\n");
	fflush(stdout);

	// Progressively write request to socket
	do {
		sentCount = write(*serverSocket, &request[sumSent], requestLength);

		if (sentCount != -1) {
			sumSent += sentCount;
		}
	} while (sumSent < requestLength);
	printf("//-------------------------------------------------------------------------------------//\n");
	printf("\n");
	fflush(stdout);
}

//------------------------------------------- Send Response to Client --------------------------------------------//
void send_to_client(char *response, int *clientSocket, int *responseSize) {
	printf("//---------------------------------- send_to_client -----------------------------------//\n");
	int sentCount = 0;
	int sumSent = 0;

	// Progressively write response to socket
	do {
		sentCount = write(*clientSocket, &response[sumSent], *responseSize);

		if (sentCount != -1) {
			sumSent += sentCount;
		}
	} while (sumSent < *responseSize);

	printf("Response\n");
	// print_bytes(response, sumSent);
	printf("\n");
	printf("%s\n", response);
	printf("\n");

	printf("//-------------------------------------------------------------------------------------//\n");
	printf("\n");
	fflush(stdout);
}

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                     Thread                                                     //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
void *thread(void *vargp) {
	printf("Enter thread\n");
	char bufIn[HTTP_REQUEST_MAX_SIZE];
	char hostname[HOSTNAME_MAX_SIZE];
	char port[PORT_MAX_SIZE];
	char uri[URI_MAX_SIZE];
	char headers[HEADERS_MAX_SIZE];
	char *request = calloc(HTTP_REQUEST_MAX_SIZE, sizeof(char));
	char *response = calloc(HTTP_RESPONSE_MAX_SIZE, sizeof(char));
	int serverSocket;
	int responseSize = 0;

	pthread_detach(pthread_self());

	while(1) {
		int clientSocket = sbuf_remove(&sbuf);
		
		// Read request from client
		read_request(&clientSocket, bufIn);

		if (parse_request(bufIn, hostname, port, uri, headers) == 0) {
			printf("Error: incomplete request\n");
		} else {
			// Assemble request
			assemble_request(request, hostname, port, uri, headers);

			// Establish a valid socket
			getServerSocket(&serverSocket, hostname, port);

			// Send request to server
			send_to_server(request, &serverSocket);

			// Read response from server
			read_server_response(response, &serverSocket, &responseSize);

			// Send response to client
			send_to_client(response, &clientSocket, &responseSize);
		}

		printf("clientSocket: %d\n", clientSocket);
		close(clientSocket);
		printf("Closed\n");
		fflush(stdout);
	}
}

//----------------------------------------------------------------------------------------------------------------//
//                                                                                                                //
//                                                      Main                                                      //
//                                                                                                                //
//----------------------------------------------------------------------------------------------------------------//
int main(int argc, char **argv) {
	printf("Enter Main\n");
	int listenSocket;
	int clientSocket;
	struct sockaddr_in ip4Address;
	struct sockaddr_storage clientAddress;
	socklen_t clientAddressLength = sizeof(struct sockaddr_storage);
	pthread_t tid; 

	// Validate input
	if (argc != 2) {
		fprintf(stderr, "usage: %s <port>\n", argv[0]);
		exit(0);
	}

	printf("Create %d threads\n", NTHREADS);

	// Blocking will be implemented later
	sbuf_init(&sbuf, SBUF_SIZE);
	for (int i = 0; i < NTHREADS; i++) {
		pthread_create(&tid, NULL, thread, NULL);
	}

	// Assembles ip4Address
	ip4Address.sin_family = AF_INET;
	ip4Address.sin_port = htons(atoi(argv[1]));
	ip4Address.sin_addr.s_addr = INADDR_ANY;

	// Gets listenSocket
	if ((listenSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket error");
		exit(EXIT_FAILURE);
	}

	// Binds listenSocket
	if (bind(listenSocket, (struct sockaddr*)&ip4Address, sizeof(struct sockaddr_in)) < 0) {
		close(listenSocket);
		perror("bind error");
		exit(EXIT_FAILURE);
	}

	// Listens on listenSocket
	if (listen(listenSocket, 100) < 0) {
		close(listenSocket);
		perror("listen error");
		exit(EXIT_FAILURE);
	} 

	// Accepts connection on listenSocket
	while (1) {
		clientSocket = accept(listenSocket, (struct sockaddr *) &clientAddress, &clientAddressLength);

		printf("//-------------------------------------------------------------------------------------//\n");
		printf("//                                                                                     //\n");
		printf("//                                   Client Socket %d                                   //\n", clientSocket);
		printf("//                                                                                     //\n");
		printf("//-------------------------------------------------------------------------------------//\n");

		sbuf_insert(&sbuf, clientSocket);
	}
}
