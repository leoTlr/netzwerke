#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

#include "helper_funcs.c"

#define ENDMSG "quit" // message to quit asking for messages to send

const size_t BUFLEN = 128;
int sockfd = -1;

// ensure closing socket when recieving SIGINT
void SIGINThandler(int signr){
	// WARNING: requires initialized global server_sockfd; probably not threadsafe
	// TODO: add SIGTERM

	if (!(signr == SIGINT)) return; // only react on SIGINT

	printf("\nSIGINT recieved. ");
	try_close(sockfd, 0);

	exit(EXIT_FAILURE);
}

int main(int argc, char **argv){

	socklen_t addrLen = sizeof(struct sockaddr_in);
	struct sockaddr_in serv_addr;

	// Check for right number of arguments
	if (argc < 3) usage(argv[0]);

	// register SIGINT-handler
	if (signal(SIGINT, SIGINThandler) == SIG_ERR){
		sys_warn("Could not register SIGINT-handler");
	}

	// Fill struct addr with IP addr, port and protocol family
	memset(&serv_addr, 0, addrLen);
	if ((serv_addr.sin_addr.s_addr = inet_addr(argv[1])) == INADDR_NONE){
		sys_err("Client Fault: NOT_AN_IP", -1, sockfd);
	}
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons((u_short)atoi(argv[2]));

	// Create new socket based on the values from
	if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		sys_err("Client Fault: SOCKET", -2, sockfd);
	}

	// connect to remote socket
	if (connect(sockfd, (struct sockaddr *) &serv_addr, addrLen) == -1){
		sys_err("Client Fault : CONNECT", -4, sockfd);
	}

	// allocate BUFLEN bytes as read-buffer
	char* msg = (char*)malloc(BUFLEN);
	size_t len = 0;
	ssize_t nread = 0;
	// strncmp only comares the first strlen(ENDMSG) bytes
	while (1){

		// reset buffer
		memset(msg, 0, BUFLEN-1);

		// read one msg into buffer
		printf("Enter String (type quit to end): ");
		nread = getline(&msg, &len, stdin);

		// substitute \n at the end of the msg with \0 to make string finish there
		// buffer still bigger but we only use the actual chars
		msg[nread-1] = '\0';

		if (strncmp(msg, ENDMSG, strlen(ENDMSG)) == 0) break;
	
		// send msg with connected socket
		printf("Sending        : %s\n", msg);
		if (sendto(sockfd, msg, strlen(msg), 0, (struct sockaddr *) &serv_addr, addrLen) == -1){
			sys_err( "Client Fault: SENDTO", -4, sockfd);
		}

		// wait for answer and print it
		printf("Listening...\n");
		char recieved_msg[BUFLEN];
		int recv_msglen = 0;
		if ((recv_msglen = recvfrom(sockfd, &recieved_msg, sizeof(recieved_msg), 0, (struct sockaddr *) &serv_addr, &addrLen)) < 0){
			sys_err("Client Fault : RECVFROM", -5, sockfd);
		}
		recieved_msg[recv_msglen] = '\0';
		printf("Server answered: %s\n", recieved_msg);
	}

	free(msg);
	try_close(sockfd, 0);
	return EXIT_SUCCESS;
}