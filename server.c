#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

#include "helper_funcs.c"

#define LISTEN_BACKLOG 1 // max connection queue length (see man listen)

const size_t BUFLEN = 128;
int server_sockfd = -1, client_sockfd;

// ensure closing socket when recieving SIGINT
void SIGINThandler(int signr){
	// WARNING: requires initialized global server_sockfd; probably not threadsafe
	// TODO: add SIGTERM

	if (!(signr == SIGINT)) return; // only react on SIGINT

	printf("\nSIGINT recieved. ");
	try_close(server_sockfd);

	exit(EXIT_FAILURE);
}

// Called with wrong arguments.
void usage(char *argv0){
	printf("usage : %s portnumber\n", argv0);
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv){

	struct sockaddr_in server_addr, client_addr;
	// correct way seems to define sockaddr_in and cast in into sockaddr in bind(), accept(),...
	socklen_t addrLen = sizeof(struct sockaddr_in);
	char recvBUF[BUFLEN];
	int len;

	// register SIGINT-handler
	if (signal(SIGINT, SIGINThandler) == SIG_ERR){
		sys_warn("Could not register SIGINT-handler");
	}

	// Check for right number of arguments
	if (argc < 2) usage(argv[0]);

	if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		sys_err("Server Fault : SOCKET", -1, server_sockfd);
	}

	// Set params so that we receive IPv4 packets from anyone on the specified port
	memset(&server_addr, 0, addrLen);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = 0 (ipv4 0.0.0.0)
	server_addr.sin_family      = AF_INET;
	server_addr.sin_port        = htons((u_short)atoi(argv[1]));

	// struct for client conection
	memset(&client_addr, 0, addrLen);
	client_addr.sin_family 	= AF_INET;
	client_addr.sin_port	= htons((u_short)atoi(argv[1]));

	if (bind(server_sockfd, (struct sockaddr *) &server_addr, addrLen) == -1){
		sys_err("Server Fault : BIND", -2, server_sockfd);
	}

	if (listen(server_sockfd, LISTEN_BACKLOG) != 0){
		sys_err("Server Fault : LISTEN", -3, server_sockfd);
	}

	// for more than one connection make one thread per connection
	while (1) {
		memset(recvBUF, 0, BUFLEN);

		// wait for incoming TCP connection (connect() call from somewhere else)
		printf("waiting for incoming connection...\n");
		if ((client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_addr, &addrLen)) < 0){ // blocking call
			sys_err("Server Fault : ACCEPT", -4, server_sockfd);
		}
		
		printf("Connection accepted from: %s\n", inet_ntoa(client_addr.sin_addr));

		// as long as data arrives, print data
		while ((len = recvfrom(client_sockfd, recvBUF, BUFLEN-1, 0, (struct sockaddr *) &client_addr, &addrLen)) >= 0){
			
			if (len == 0){
				printf("Closing connection to %s\n", inet_ntoa(client_addr.sin_addr));
				try_close(client_sockfd);
				break;
			}
			
			// Write data to stdout
			printf("Received from %s: %s\n", inet_ntoa(client_addr.sin_addr), recvBUF);
			//write(STDOUT_FILENO, recvBUF, len);
			memset(recvBUF, 0, BUFLEN-1); // reset recieve buffer
		}

		if (len < 0) {
			try_close(client_sockfd);
			sys_err("Server Fault : RECVFROM", -5, server_sockfd);
		}

		printf("\n");
	}

	try_close(server_sockfd);
	return EXIT_SUCCESS;
}
