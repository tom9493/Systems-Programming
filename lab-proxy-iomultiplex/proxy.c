#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define MAXEVENTS 10

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10.15; rv:97.0) Gecko/20100101 Firefox/97.0";
static int verbose = 0;
static int set = 0;

struct client_info {
	int cfd;
	int state;
	int sfd;
	char desc[1024];
	char cRecv[2500];
	char sRecv[MAX_OBJECT_SIZE];
	char sSend[2500];
	int cBytesRead;	
	int sBytesToSend;
	int sBytesSent;
	int sBytesRead;
	int cBytesSent;
};

void sigint_handler(int signum) {
	printf("In sigint_handler\n");
	fflush(stdout);
	set = 1;
}

int open_lfd(struct sockaddr_in);
int all_headers_received(char *);
int parse_request(char *, char *, char *, char *, char *, char *);
int getSSFD(char *, char *);
void test_parser();
void print_bytes(unsigned char *, int);
void handle_new_clients(int, struct client_info*, struct epoll_event);
void handle_client(int, struct client_info*, struct epoll_event);
void getServerRequest(char *, char *, char *, char *, char *);
void deregister(struct client_info*);

int main(int argc, char **argv) {
	struct sigaction sigact;
        struct sockaddr_in addr;
        struct client_info *listener;
        struct client_info *active_client;
        struct epoll_event event;
        struct epoll_event *events;
        int efd, listen_socket, n;

	if (argv[2] != NULL) { if (!strcmp(argv[2], "-v")) { verbose = 1; } }
        if (argv[1] == NULL) { printf("Need port number as first argument. Exiting...\n"); fflush(stdout); exit(1); }
	
	sigact.sa_flags = SA_RESTART;
	sigact.sa_handler = sigint_handler;
	sigaction(SIGINT, &sigact, NULL);

       	if ((efd = epoll_create1(0)) < 0) { fprintf(stderr, "error creating epoll fd\n"); exit(1); }

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[1]));	
	listen_socket = open_lfd(addr);

        // allocate memory for a new struct client_info, and populate it with info for the listening socket
        listener = calloc(1, sizeof(struct client_info));
        listener->cfd = listen_socket;
        sprintf(listener->desc, "Listen file descriptor (accepts new clients)");

        // register the listening file descriptor for incoming events using edge-triggered monitoring
        event.data.ptr = listener;
        event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, listener->cfd, &event) < 0) {
                fprintf(stderr, "error adding event\n");
                exit(1);
        }

        events = calloc(MAXEVENTS, sizeof(struct epoll_event));
	
	while (1) {
		if ((n = epoll_wait(efd, events, MAXEVENTS, 1)) < 0) { fprintf(stderr, "error adding event\n"); exit(1); }	

		if (n == 0 && set == 1) { printf("int handler called. breaking.\n"); fflush(stdout); break; }

                for (int i = 0; i < n; i++) {
                        active_client = (struct client_info *)(events[i].data.ptr);

                        if (verbose) { printf("New event for %s\n", active_client->desc); fflush(stdout); }

                        if ((events[i].events & EPOLLERR) || (events[i].events & EPOLLHUP) || (events[i].events & EPOLLRDHUP)) {
                                fprintf(stderr, "epoll error on %s\n", active_client->desc);
                                deregister(active_client);
                        }		

                        if (listener->cfd == active_client->cfd) { handle_new_clients(efd, active_client, event); } 
			else { handle_client(efd, active_client, event); }
		}	
	}
	free(listener);
	free(events);
	return 0;
}

void handle_client(int efd, struct client_info *client, struct epoll_event event) {
 	char buf[2000];
	
	if (verbose) { printf("In handle client\nClient description and state:\n%s\nstate: %d\n", client->desc, client->state); fflush(stdout); }

	if (client->state == 0)	{ // Read request
        	char method[32], hostname[128], port[16], path[128], headers[4096];
		int bytesRead;
	
		if (client->cBytesRead == 0) {	
			memset(buf, '\0', sizeof(buf));
			memset(method, '\0', sizeof(method));
			memset(hostname, '\0', sizeof(hostname));
			memset(port, '\0', sizeof(port));
			memset(path, '\0', sizeof(path));
			memset(headers, '\0', sizeof(headers));
		}
		
		bytesRead = -1;
		while (strstr(client->cRecv, "\r\n\r\n") == NULL && bytesRead != 0) {
			memset(buf, '\0', sizeof(buf));
			if ((bytesRead = recv(client->cfd, buf, sizeof(buf), 0)) == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) { return; }
				fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno); fflush(stderr);
				deregister(client);				
			}
			memcpy(client->cRecv + client->cBytesRead, buf, bytesRead);
			client->cBytesRead += bytesRead;
		}
		if (bytesRead == 0) { memcpy(client->cRecv + client->cBytesRead, "\r\n\r\n", 8); printf("Had to add rn\n"); fflush(stdout); }

		if (!parse_request(client->cRecv, method, hostname, port, path, headers)) { printf("REQUEST INCOMPLETE... Exiting\n"); fflush(stdout); exit(1); }
		
		getServerRequest(method, hostname, port, path, client->sSend);
		if (verbose) { printf("serverRequest after all strcpy's:\n%s\nClient #: %d\n", client->sSend, client->cfd); fflush(stdout); }
		
		client->sBytesToSend = strlen(client->sSend);
		client->sfd = getSSFD(hostname, port); // Server socket file-descriptor
		event.data.ptr = client;
		event.events = EPOLLOUT | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, client->sfd, &event) < 0) { fprintf(stderr, "error adding event\n"); exit(1); }
		client->state = 1;
		return;
	}
		
	else if (client->state == 1)	{ // Send request
		int bytesSent;
		
		bytesSent = 0;
		while (client->sBytesToSend != client->sBytesSent) {
			if ((bytesSent = write(client->sfd, client->sSend + client->sBytesSent, strlen(client->sSend) - client->sBytesSent)) == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) { return; }
				fprintf(stderr, "send: %s (%d)\n", strerror(errno), errno); fflush(stderr);
				deregister(client);
			}
			client->sBytesSent += bytesSent;
		}
	
		event.data.ptr = client;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_MOD, client->sfd, &event) < 0) { fprintf(stderr, "error adding event\n"); exit(1); }
		client->state = 2;
		return;
	}

	else if (client->state == 2)	{ // Read response
		int bytesRead;
		
		bytesRead = -1;
		while (bytesRead != 0) {
			memset(buf, '\0', sizeof(buf));
			if ((bytesRead = recv(client->sfd, buf, sizeof(buf), 0)) == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) { return; }
				fprintf(stderr, "recv ERRORRRRRR: %s (%d)\n", strerror(errno), errno); fflush(stderr);
				deregister(client);
			}
			if (verbose) { printf("\nClient #: %d\nBytesRead: %d\n\nTotal request so far:\n%s\n\n", client->cfd, bytesRead, client->sRecv); fflush(stdout); }
			memcpy(client->sRecv + client->sBytesRead, buf, bytesRead);
			client->sBytesRead += bytesRead;
		}

		if (verbose) { 
			printf("serverResponse after recv:\n%s\nlength of serverResponse: %ld\n", client->sRecv, strlen(client->sRecv));
		       	printf("client->sBytesRead: %d\n", client->sBytesRead); 
			fflush(stdout); 
		}
	
		close(client->sfd);
		event.data.ptr = client;
		event.events = EPOLLOUT | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_MOD, client->cfd, &event) < 0) { fprintf(stderr, "error adding event\n"); exit(1); }
		client->state = 3;
		return;
	}
	
	else if (client->state == 3)	{ // Send response
		int bytesSent;

		bytesSent = 0;
		while (client->cBytesSent != client->sBytesRead) {
			if ((bytesSent = write(client->cfd, client->sRecv + client->cBytesSent, client->sBytesRead - client->cBytesSent)) == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) { return; }
				fprintf(stderr, "send: %s (%d)\n", strerror(errno), errno); fflush(stderr);
				deregister(client);
			}
			client->cBytesSent += bytesSent;
		}
		
		close(client->cfd);
		free(client);
		if (verbose) { printf("cfd closed. client freed. Success!\n"); fflush(stdout); }
		return;
	}
}

void handle_new_clients(int efd, struct client_info *listener, struct epoll_event event) {
	int connfd;
	struct client_info *new_client;
	struct sockaddr_storage clientaddr;
	socklen_t clientlen = sizeof(struct sockaddr_storage);

	//if (verbose) { printf("In handle_new_clients\n"); fflush(stdout); }

	while (1) {
		connfd = accept(listener->cfd, (struct sockaddr *)&clientaddr, &clientlen);

		if (connfd < 0) {
			if (errno == EWOULDBLOCK || errno == EAGAIN) { break; } // No more clients ready to accept
			else { perror("accept"); exit(EXIT_FAILURE); } // Error occured
		}

		// set client file descriptor non-blocking
		if (fcntl(connfd, F_SETFL, fcntl(connfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
			fprintf(stderr, "error setting socket option\n");
			exit(1);
		}

		// allocate memory for a new struct client_info, and populate it with info for the new client
		new_client = (struct client_info *)calloc(1, sizeof(struct client_info));
		new_client->cfd = connfd;
		sprintf(new_client->desc, "Client with file descriptor %d", connfd);

		// register the client file descriptor for incoming events using edge-triggered monitoring
		event.data.ptr = new_client;
		event.events = EPOLLIN | EPOLLET;
		if (epoll_ctl(efd, EPOLL_CTL_ADD, connfd, &event) < 0) {
			fprintf(stderr, "error adding event\n");
			exit(1);
		}
	}
}

int open_lfd(struct sockaddr_in addr) {
        int lfd = socket(AF_INET, SOCK_STREAM, 0);
        int optval = 1;
        setsockopt(lfd, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
       
        // set listening file descriptor non-blocking
        if (fcntl(lfd, F_SETFL, fcntl(lfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
                fprintf(stderr, "error setting socket option\n");
                exit(1);
        }

	bind(lfd, (struct sockaddr *)&addr, sizeof(addr));
        listen(lfd, MAXEVENTS);
	
        return lfd;
}

void deregister(struct client_info *client) {
	close(client->cfd);
	close(client->sfd);
	free(client);
}

int all_headers_received(char *request) {
	char *end = "\r\n\r\n";
        if (strstr(request, end) == NULL) return 0;
        return 1;
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

       // if (verbose) { printf("hostname and port before getaddrinfo:\n%s:%s\n", hostname, port); fflush(stdout); }

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

	if (fcntl(ssfd, F_SETFL, fcntl(ssfd, F_GETFL, 0) | O_NONBLOCK) < 0) {
                fprintf(stderr, "error setting socket option\n");
                exit(1);
        }

        freeaddrinfo(result);
        return ssfd;
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

        //if (verbose) { printf("Entering parse_request. This is the request to parse:\n%s\n", request); fflush(stdout); }

        method_start = request;
        method_end = strchr(request, ' ');

        //if (verbose) { printf("check1\n"); fflush(stdout); }

        strncpy(method, method_start, method_end - method_start);

        //if (verbose) { printf("check2\n"); fflush(stdout); }

        check = method_end[1];

        //if (verbose) { printf("localhost check: %c\n", check); fflush(stdout); }

        if (check == '/') {
               // if (verbose) { printf("Only path specified. Localhost.\n"); fflush(stdout); }

                strcpy(hostname, "localhost");

                port_start = strstr(request, "Host: ") + 6;
                port_start = strchr(port_start, ':') + 1;
                port_end = strstr(port_start, "\r\n");
                strncpy(port, port_start, port_end - port_start);
	
		path_start = strchr(request, '/');
                path_end = strchr(path_start, ' ');
                strncpy(path, path_start, path_end - path_start);

	} else {

               // if (verbose) { printf("url given after method\n"); fflush(stdout); }

                strcpy(firstLine, request);
                firstLine_end = strstr(firstLine, "\r\n");
                firstLine_end[0] = 0;

               // if (verbose) { printf("firstLine of client request:\n%s\n", firstLine); fflush(stdout); }

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
       // if (verbose) { printf("path after strncpy: %s\n", path); fflush(stdout); }

        headers_start = strstr(request, "Host:");
        headers_end = strstr(request, "\r\n\r\n");
        strncpy(headers, headers_start, headers_end - headers_start);

       // if (verbose) { printf("headers after strncpy:\n%s\n", headers); printf("huh?\n"); fflush(stdout); }

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
		printf("Testing:\n%s\n", reqs[i]);
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
