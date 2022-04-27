#define USERID 1823692024
#define BUFSIZE 8

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <errno.h>

int verbose = 0;

void print_bytes(unsigned char *bytes, int byteslen);

int main(int argc, char *argv[]) {
	struct addrinfo hints;
	struct addrinfo *result, *rp;
	struct sockaddr_in ipv4addr_remote;
	struct sockaddr_in ipv4addr_local;
	struct sockaddr_in6 ipv6addr_remote;
	struct sockaddr_in6 ipv6addr_local;
	char zero = 0;
	char treasure[1024];
	char* portString = argv[2];
	unsigned char buf[BUFSIZE];
	unsigned char buf2[64];
	unsigned char n;
	unsigned int id = 0x6cb354f8;
	int sfd, s;
	int size = 0;
	int i = 0;
	int af = AF_INET;
	
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = af;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = 0;
	hints.ai_protocol = 0;
	
	s = getaddrinfo(argv[1], portString, &hints, &result);
	if (s != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		exit(EXIT_FAILURE);
	}
	
	for (rp = result; rp != NULL; rp = rp->ai_next) {
		sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (sfd == -1) { continue; }
		if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1) { break; }
		close(sfd);
	}

	if (rp == NULL) {
		fprintf(stderr, "Could not connect\n");
		exit(EXIT_FAILURE);
	}

	ipv4addr_remote = *(struct sockaddr_in *)rp->ai_addr;
	ipv6addr_remote = *(struct sockaddr_in6*)rp->ai_addr;
	
	freeaddrinfo(result);
	bzero(buf, BUFSIZE);
	
	int level = atoi(argv[3]);

	memcpy(&buf[0], &zero, 1);     	// zero
	memcpy(&buf[1], &level, 1);	// level
	memcpy(&buf[2], &id, 4);	// user id
	memcpy(&buf[6], &argv[4], 2);	// seed

	send(sfd, buf, sizeof buf, 0);
	int nrecv = recv(sfd, buf2, 64, 0);
	n = buf2[0];
	memcpy(&treasure[size], &buf2[1], n);
	size += n;

	if (verbose)	{	 
		memcpy(&treasure[size], &zero, 1);   // Add the zero bytes so printf works
		printf("treasure 00: %s\n", treasure);
		print_bytes(buf2, 64);	
	}
      	
	while (n != 0) {
		char opCode;
		char* nonceString = argv[2];	
		unsigned int nonce;
		unsigned short portOrBytes;
		unsigned short m;
		
		memcpy(&opCode, &buf2[n+1], 1);
		memcpy(&portOrBytes, &buf2[n+2], 2);
		memcpy(&nonce, &buf2[n+4], 4);
		
		portOrBytes = ntohs(portOrBytes);
		
		nonce = ntohl(nonce);
		nonce += 1;
		nonce = htonl(nonce);
		*(unsigned int*) nonceString = nonce;
		
		if (verbose) {
			printf("opcode: %d\n", opCode);
			fflush(stdout);
		}

		if (opCode > 4 || opCode < 0) {
			printf("opCode out of range: %d\n", opCode);
			fflush(stdout);
			exit(1);
		}

		if (n > 127) {
			printf("n > 127\n");
			fflush(stdout);
			print_bytes(buf2, 8);
			exit(1);
		}

		if (opCode == 1) {
			// Changing the server port we talk to
			ipv4addr_remote.sin_port = htons(portOrBytes);
			ipv6addr_remote.sin6_port = htons(portOrBytes);
		}	

		else if (opCode == 2) {
			// Changing client port we talk from
			close(sfd);
			if (af == AF_INET) {
				if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) < -1) {
					perror("socket() 4");
				}
				
				ipv4addr_local.sin_family = af;
				ipv4addr_local.sin_port = htons(portOrBytes);
				ipv4addr_local.sin_addr.s_addr = 0;

				if (bind(sfd, (struct sockaddr *)&ipv4addr_local, sizeof(struct sockaddr_in)) < 0) {
					perror("bind() 4");
				}	
			} else {
				if ((sfd = socket(AF_INET6, SOCK_STREAM, 0)) < -1) {
					perror("socket() 6");
				}

				ipv6addr_local.sin6_family = af;
				ipv6addr_local.sin6_port = htons(portOrBytes);
				bzero(ipv6addr_local.sin6_addr.s6_addr, 16);

				if (bind(sfd, (struct sockaddr *)&ipv6addr_local, sizeof(struct sockaddr_in6)) < 0) {
					perror("bind() 6");
				}
			}
		}

		else if (opCode == 3) {
			m = portOrBytes;
			nonce = ntohl(nonce);
			nonce = 0;
			unsigned int fromlen = sizeof(ipv4addr_remote);
			unsigned int fromlen6 = sizeof(ipv6addr_remote);
		
			if (getsockname(sfd, (struct sockaddr *)&ipv4addr_local, &fromlen) < 0) {
				perror("getsockname() ipv4");
			}
			if (getsockname(sfd, (struct sockaddr *)&ipv6addr_local, &fromlen6) < 0) {
				perror("getsockname() ipv6");
			}

			close(sfd);
			if (af == AF_INET) {
				if ((sfd = socket(AF_INET, SOCK_DGRAM, 0)) < -1) {
					perror("socket() 4");
				}
				if (bind(sfd, (struct sockaddr *)&ipv4addr_local, sizeof(struct sockaddr_in)) < 0) {
					perror("bind() 4");
				}
			} else {
				if ((sfd = socket(AF_INET6, SOCK_STREAM, 0)) < -1) {
					perror("socket() 6");
				}
				if (bind(sfd, (struct sockaddr *)&ipv6addr_local, sizeof(struct sockaddr_in6)) < 0) {
					perror("bind() 6");
				}
			}
			
			struct sockaddr_in temp_st4;
			struct sockaddr_in6 temp_st6;	
			char* dumb_buf;

			for (int j = 0; j < m; ++j) {
				if (af == AF_INET) {
					if (recvfrom(sfd, dumb_buf, 1, 0, (struct sockaddr *)&temp_st4, &fromlen) < 0) {
						perror("recvfrom() 4");
					}
					unsigned short tempPort = ntohs(temp_st4.sin_port);	
					nonce += tempPort;
				} else {
					if (recvfrom(sfd, dumb_buf, 1, 0, (struct sockaddr*)&temp_st6, &fromlen6) < 0) {
						perror("recvfrom() 6");
					}
					unsigned short tempPort6 = ntohs(temp_st6.sin6_port);	
					nonce += tempPort6;

				}	
			}
			nonce += 1;
			nonce = htonl(nonce);
			*(unsigned int*) nonceString = nonce;

		}

		else if (opCode == 4) {
			close(sfd);
			sprintf(portString, "%d", portOrBytes);
			
			if (af == AF_INET) {
				af = AF_INET6;
				hints.ai_family = af;
			}
			else { 
				af = AF_INET;
				hints.ai_family = af;
			}
		
			s = getaddrinfo(argv[1], portString, &hints, &result);
			if (s != 0) {
				fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
				exit(EXIT_FAILURE);
			}

			for (rp = result; rp != NULL; rp = rp->ai_next) {
				sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
				if (sfd == -1) { continue; }
				else { break; }
			}
			
			//sfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
			ipv4addr_remote = *(struct sockaddr_in *)rp->ai_addr;
			ipv6addr_remote = *(struct sockaddr_in6*)rp->ai_addr;


			ipv4addr_remote.sin_port = htons(portOrBytes);
			ipv6addr_remote.sin6_port = htons(portOrBytes);

			freeaddrinfo(result);

		}
	
		if (af == AF_INET) {
			if (connect(sfd, (struct sockaddr *)&ipv4addr_remote, sizeof(struct sockaddr_in)) < 0) {
				perror("connect() 4");
			}
		} else {
			if (connect(sfd, (struct sockaddr *)&ipv6addr_remote, sizeof(struct sockaddr_in6)) < 0) {
				perror("connect() 6");
			}
		}
		
		send(sfd, nonceString, sizeof(int), 0);
			
		nrecv = recv(sfd, buf2, 64, 0);
		if (nrecv == 0 || nrecv == -1) { 
			fprintf(stderr, "recv: %s (%d)\n", strerror(errno), errno);
			exit(0); 
		}

		if (verbose) { print_bytes(buf2, 64); }

		n = buf2[0];	
		memcpy(&treasure[size], &buf2[1], n);
		size += n;
	
		if (verbose) {
			printf("treasure %d: %s\n", i, treasure);
			++i;
		}
	}
		
	memcpy(&treasure[size], &zero, 1);
	printf("%s\n", treasure);
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

