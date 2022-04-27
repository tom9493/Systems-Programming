#include "sbuf.h"
#include "sbuf.c"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>


/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 8
#define SBUFSIZE 6

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";
static int verbose = 0;

int all_headers_received(char *);
int parse_request(char *, char *, char *, char *, char *, char *);
int open_sfd(struct sockaddr_in addr);
int getSSFD(char *, char *);
int handle_server(int, char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
void *handle_client();
void getServerRequest(char *, char *, char *, char *, char *);
sbuf_t sbuf;

int main(int argc, char *argv[]) {
	struct sockaddr_in addr;
	int sock, csfd; 		// Client socket file-descriptor
	pthread_t tid;	

	if (argv[2] != NULL) { if (!strcmp(argv[2], "-v")) { verbose = 1; } }
	if (argv[1] == NULL) { printf("Need port number as first argument. Exiting...\n"); fflush(stdout); exit(1); }

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[1]));
	socklen_t size = sizeof(addr);
	sock = open_sfd(addr);

	sbuf_init(&sbuf, SBUFSIZE);
	for (int i = 0; i < NTHREADS; i++) {
		pthread_create(&tid, NULL, handle_client, NULL);
	}

	while(1) {
		csfd = accept(sock, (struct sockaddr *)&addr, &size);
		if (verbose) { printf("Accepted!\n"); }
		sbuf_insert(&sbuf, csfd);
	}
	
	return 0;
}

int open_sfd(struct sockaddr_in addr) {
	int sock = socket(AF_INET, SOCK_STREAM, 0);
	int optval = 1;

	setsockopt(sock, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
	bind(sock, (struct sockaddr *)&addr, sizeof(addr));
	listen(sock, 10);
	
	return sock;
}

void *handle_client(void* csfdp) {
	char request[2500];
	char serverRequest[2500];	
	char serverResponse[MAX_OBJECT_SIZE];
	char buf[1000];
	char method[32], hostname[128], port[16], path[128], headers[4096];
	int bytesRead, totalBytesRead, ssfd;
	int csfd;

	pthread_detach(pthread_self());
	csfd = sbuf_remove(&sbuf);
	
	if (verbose) { printf("Entering handle_client\n"); fflush(stdout); }

	// Set memory
	memset(request, '\0', sizeof(request));	
	memset(buf, '\0', sizeof(buf));
	memset(method, '\0', sizeof(method));
	memset(hostname, '\0', sizeof(hostname));
	memset(port, '\0', sizeof(port));
	memset(path, '\0', sizeof(path));
	memset(headers, '\0', sizeof(headers));
	memset(serverResponse, '\0', sizeof(serverResponse));
	
	bytesRead = -1;
	totalBytesRead = 0;	
	// Get initial request from client
	while (strstr(request, "\r\n\r\n") == NULL && bytesRead != 0) {
		memset(buf, '\0', sizeof(buf));
		if ((bytesRead = recv(csfd, buf, sizeof(buf), 0)) == -1) {
			fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
			exit(7);
		}
		if (verbose) { printf("buf and bytesRead from client:\n\%s -- %d\n", buf, bytesRead); fflush(stdout); }
		memcpy(request + totalBytesRead, buf, bytesRead);
		totalBytesRead += bytesRead;
	}
	if (bytesRead == 0) { memcpy(request + totalBytesRead, "\r\n\r\n", 8); printf("Had to add rn\n"); fflush(stdout); }
	if (verbose) { printf("request after recv:\n%s\n", request); fflush(stdout); }
	
	// Parse request from client
	if (!parse_request(request, method, hostname, port, path, headers)) { printf("REQUEST INCOMPLETE... Exiting\n"); fflush(stdout); exit(1); }
	if (verbose) { printf("Method, path, and headers after parse_request:\n"); } 
	printf("%s %s HTTP/1.0\n%s\n", method, path, headers); fflush(stdout);

	// Assign variables from client to request server can take
	getServerRequest(method, hostname, port, path, serverRequest);
	if (verbose) { printf("serverRequest after all strcpy's:\n%s\n", serverRequest); fflush(stdout); }

	// Create server socket file descriptor
	ssfd = getSSFD(hostname, port); // Server socket file-descriptor
	if (verbose) { printf("ssfd received: %d\n", ssfd); fflush(stdout); }

	// Send appropriate request to server and receive response back
	totalBytesRead = handle_server(ssfd, serverRequest, serverResponse);

	if (verbose) { printf("strlen(serverResponse) and totalBytesRead:\n%ld\n%d\n\n", strlen(serverResponse), totalBytesRead); fflush(stdout); }
	// Send response as is back to client
	send(csfd, serverResponse, totalBytesRead, 0);
	close(csfd);

	return NULL;
}

int handle_server(int ssfd, char* serverRequest, char* serverResponse) {
	char buf[1000];
	int bytesRead, sendCheck, totalBytesRead;

	if (verbose) { printf("Entering handle_server\n\n"); fflush(stdout); }

	if ((sendCheck = write(ssfd, serverRequest, strlen(serverRequest))) == -1) {
		fprintf(stderr, "send: %s (%d)\n", strerror(errno), errno);
		fflush(stderr);
		exit(4);
	}

	memset(buf, '\0', sizeof(buf));
	totalBytesRead = 0;	
	bytesRead = -1;
	while (bytesRead != 0) {// || strchr(serverResponse, '\0')) { 
		if ((bytesRead = recv(ssfd, buf, sizeof(buf), 0)) == -1) {
			fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
			fflush(stderr);
			exit(5);
		}
		if (verbose) { printf("\nbuf, bytesRead, and totalBytesRead:\n%s\n%d\n%d\n", buf, bytesRead, totalBytesRead); fflush(stdout); }
		memcpy(serverResponse + totalBytesRead, buf, bytesRead);
		totalBytesRead += bytesRead;
	}

	if (verbose) { printf("serverResponse after recv:\n%s\nlength of serverResponse: %ld\ntotalBytesRead: %d\n", serverResponse, strlen(serverResponse), totalBytesRead); fflush(stdout); }

	close(ssfd);

	return totalBytesRead;
}



void getServerRequest(char *method, char *hostname, char *port, char *path, char* serverRequest) {
	char *con = "Connection: close\r\n";
	char *pcon = "Proxy-Connection: close\r\n\r\n";
	char *version = "HTTP/1.0\r\n";
	char *line = "\r\n";
	char *p = serverRequest;

	if (verbose) { printf("Entered getServerRequest\n"); } 

	strcpy(p, method);
	p += strlen(method);
	strcpy(p, " ");
	p += 1;
	strcpy(p, path);
	p += strlen(path);
	strcpy(p, " ");
	p += 1;
	strcpy(p, version);
	p += strlen(version);
	strcpy(p, "Host: ");
	p += 6;
	strcpy(p, hostname);
	p += strlen(hostname);
	
	if (strcmp(port,"80")) {
		strcpy(p, ":");
		p += 1;
		strcpy(p, port);
		p += strlen(port);
	}

	strcpy(p, line);
	p += strlen(line);
	strcpy(p, user_agent_hdr);
	p += strlen(user_agent_hdr);
	strcpy(p, line);
	p += strlen(line);
	strcpy(p, con);
	p += strlen(con);
	strcpy(p, pcon);
}

int getSSFD(char* hostname, char* port) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	int ssfd, s;

	if (verbose) { printf("Entering getSSFD\n"); fflush(stdout); }
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;

	if (verbose) { printf("hostname and port before getaddrinfo:\n%s:%s\n", hostname, port); fflush(stdout); }

	s = getaddrinfo(hostname, port, &hints, &result);
	if (s != 0) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s)); exit(2); }

	for (rp = result; rp != NULL; rp = rp->ai_next) {
		ssfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (ssfd == -1) { continue; }
		if (connect(ssfd, rp->ai_addr, rp->ai_addrlen) != -1) { break; }
		printf("Connection failed... closing ssfd\n");
		close(ssfd);
	}

	if (rp == NULL) { fprintf(stderr, "Could not connect\n"); exit(3); }

	freeaddrinfo(result);

	return ssfd;
}

int parse_request(char *request, char *method,
		char *hostname, char *port, char *path, char *headers) {
	char *method_start;
	char *method_end;	
	char *host_start;
	char *host_end;
	char *port_start;
	char *port_end;
	char *path_start;
	char *path_end;
	char *headers_start;
	char *headers_end;
	char *firstLine_end;
	char firstLine[250];
	char check;

	if (all_headers_received(request) == 0) { return 0; }
	
	if (verbose) { printf("Entering parse_request. This is the request to parse:\n%s\n", request); fflush(stdout); }

	method_start = request;
	method_end = strchr(request, ' ');

	if (verbose) { printf("check1\n"); fflush(stdout); }
	
	strncpy(method, method_start, method_end - method_start);

	if (verbose) { printf("check2\n"); fflush(stdout); }

	check = method_end[1];	
	
	if (verbose) { printf("localhost check: %c\n", check); fflush(stdout); }

	if (check == '/') {
		if (verbose) { printf("Only path specified. Localhost.\n"); fflush(stdout); }

		strcpy(hostname, "localhost");
		
		port_start = strstr(request, "Host: ") + 6;
		port_start = strchr(port_start, ':') + 1;
		port_end = strstr(port_start, "\r\n");
		strncpy(port, port_start, port_end - port_start);
		
		path_start = strchr(request, '/');
		path_end = strchr(path_start, ' ');
		strncpy(path, path_start, path_end - path_start);

	} else {

		if (verbose) { printf("url given after method\n"); fflush(stdout); }

		strcpy(firstLine, request);
		firstLine_end = strstr(firstLine, "\r\n");
		firstLine_end[0] = 0;

		if (verbose) { printf("firstLine of client request:\n%s\n", firstLine); fflush(stdout); }

		host_start = strstr(firstLine, "//") + 2;

		if (strchr(host_start, ':') == NULL) { 
			host_end = strchr(host_start, '/');
			strcpy(port, "80");
		} else {
			host_end = strchr(host_start, ':');
		
			port_start = strchr(host_start, ':') + 1;
			port_end = strchr(host_end, '/');
		
			strncpy(port, port_start, port_end - port_start);
		}
	
		strncpy(hostname, host_start, host_end - host_start);

		path_start = strchr(host_start, '/');
		path_end = strchr(host_start, ' ');
	
		strncpy(path, path_start, path_end - path_start);
	}
	if (verbose) { printf("path after strncpy: %s\n", path); fflush(stdout); }
	
	headers_start = strstr(request, "Host:");
	headers_end = strstr(request, "\r\n\r\n");
	strncpy(headers, headers_start, headers_end - headers_start);

	if (verbose) { printf("headers after strncpy:\n%s\n", headers); printf("huh?\n"); fflush(stdout); } 
	
	return 1;
}

int all_headers_received(char *request) {	
	char *end = "\r\n\r\n";
	if (strstr(request, end) == NULL) return 0; 
	return 1;
}

void test_parser() {
	int i;
	char method[16], hostname[64], port[8], path[64], headers[1024];

       	char *reqs[] = {
		"GET http://www.example.com/index.html HTTP/1.0\r\n"
		"Host: www.example.com\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html?foo=1&bar=2 HTTP/1.0\r\n"
		"Host: www.example.com:8080\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://localhost:1234/home.html HTTP/1.0\r\n"
		"Host: localhost:1234\r\n"
		"User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:68.0) Gecko/20100101 Firefox/68.0\r\n"
		"Accept-Language: en-US,en;q=0.5\r\n\r\n",

		"GET http://www.example.com:8080/index.html HTTP/1.0\r\n",

		NULL
	};
	
	for (i = 0; reqs[i] != NULL; i++) {
		printf("Testing %s\n", reqs[i]);
		if (parse_request(reqs[i], method, hostname, port, path, headers)) {
			printf("METHOD: %s\n", method);
			printf("HOSTNAME: %s\n", hostname);
			printf("PORT: %s\n", port);
			printf("HEADERS: %s\n", headers);
		} else {
			printf("REQUEST INCOMPLETE\n");
		}
	}
}

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
