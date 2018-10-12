#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "helper_funcs.c"

#define MAX_CONNECTIONS 10
#define MAX_THREADS MAX_CONNECTIONS
#define LISTEN_BACKLOG 10 // max connection queue length (see man listen)
#define BUFSIZE 256

typedef struct {
	int connfd;
	int clientnr;
	struct sockaddr_in client_addr;
	socklen_t addrlen;
} thread_args_t;

// thread body and exit handler
static void connection_thread(void *);
static void thread_exithdlr(void *);

pthread_t started_threads[MAX_THREADS]; // set data type qould be nice here
int server_sockfd = -1, client_sockfd;

// ensure closing socket when recieving SIGINT
static void sighandler(int signo){
	// exiting normally (with exit(0)) should invoke the threads exithandlers
	// EDIT: seems not to; TODO
	switch (signo){
		case SIGINT:
			printf("\nSIGINT recieved. Closing sockets\n");
			try_close(server_sockfd);
			exit(EXIT_SUCCESS);
		case SIGTERM:
			printf("\nSIGTERM recieved. Closing server socket: ");
			try_close(server_sockfd);
			exit(EXIT_FAILURE);
		default:
			break;
	}
}

// Called with wrong arguments.
void usage(char *argv0){
	printf("usage : %s portnumber\n", argv0);
	exit(EXIT_SUCCESS);
}

int main(int argc, char **argv){

	int started_threads_counter = 0;
	struct sockaddr_in server_addr, client_addr; // getting casted to sockaddr on usage
	socklen_t addrlen = sizeof(struct sockaddr_in);

	// register SIGINT-handler
    struct sigaction sa;
	sigemptyset(&sa.sa_mask);
    sa.sa_handler = &sighandler;
    sa.sa_flags= 0;
    if ((sigaction(SIGINT, &sa, NULL)) < 0){
		sys_warn("Could not register SIGINT-handler");
    }

	// Check for right number of arguments
	if (argc < 2) usage(argv[0]);

	if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		sys_err("Server Fault : SOCKET", -1, server_sockfd);
	}

	// Set params so that we receive IPv4 packets from anyone on the specified port
	memset(&server_addr, 0, addrlen);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = 0 (ipv4 0.0.0.0)
	server_addr.sin_family      = AF_INET;
	server_addr.sin_port        = htons((u_short)atoi(argv[1]));

	// struct for client conection
	memset(&client_addr, 0, addrlen);
	client_addr.sin_family 	= AF_INET;
	client_addr.sin_port	= htons((u_short)atoi(argv[1]));

	if (bind(server_sockfd, (struct sockaddr *) &server_addr, addrlen) == -1){
		sys_err("Server Fault : BIND", -2, server_sockfd);
	}

	if (listen(server_sockfd, LISTEN_BACKLOG) != 0){
		sys_err("Server Fault : LISTEN", -3, server_sockfd);
	}

	printf("waiting for incoming connections...\n");
	while (1) {
		
		// wait for incoming TCP connection (connect() call from somewhere else)
		if ((client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_addr, &addrlen)) < 0){ // blocking call
			sys_err("Server Fault : ACCEPT", -4, server_sockfd);
		}

		// prepare args and create separate thread to handle connection
		const thread_args_t th_args = {client_sockfd, started_threads_counter, client_addr, addrlen};
		pthread_create(&started_threads[started_threads_counter++], NULL, (void *) &connection_thread, (void *) &th_args);	
		// TODO adapt thread array or how to place them in	
	}

	try_close(server_sockfd);
	return EXIT_SUCCESS;
}

static void connection_thread(void * th_args) {

	// unsure: values in th_args could get overwritten in main thread, only the pointer is const
	// make local copy of args
	thread_args_t* args = (thread_args_t*)malloc(sizeof(thread_args_t));
	memcpy(args, (thread_args_t*) th_args, sizeof(thread_args_t));

	int msg_len = 0;

	// initialize buffer
	char recvBUF[BUFSIZE];
	memset(recvBUF, 0, BUFSIZE);

	// setup exit-handler
	pthread_cleanup_push((void *) &thread_exithdlr, (void *) args);

	printf("Connection accepted from: %s (client %d)\n", inet_ntoa(args->client_addr.sin_addr), args->clientnr);

	// as long as data arrives, print data
	while ((msg_len = recvfrom(args->connfd, &recvBUF, BUFSIZE-1, 0, (struct sockaddr *) &args->client_addr, &args->addrlen)) >= 0) {
		
		if (msg_len == 0){
			printf("Closing connection to client %d (%s)\n", args->clientnr, inet_ntoa(args->client_addr.sin_addr));
			break;
		}

		if (msg_len < 0) {
			sys_warn("Server Fault : RECVFROM");
			pthread_exit((void *) pthread_self());
		}

		recvBUF[msg_len] = '\0'; // cut string to proper length

		// Write data to stdout
		printf("client %d (%s): %s\n", args->clientnr, inet_ntoa(args->client_addr.sin_addr), recvBUF);

		memset(recvBUF, 0, BUFSIZE-1); // reset recieve buffer and continue
	}

	// remove exit_handler (and run it)
	pthread_cleanup_pop(1);
	pthread_exit((void *)pthread_self());
}

static void thread_exithdlr(void * th_args) {
	thread_args_t *args = (thread_args_t *) th_args;
	printf("client %d: ", args->clientnr);
	try_close(args->connfd);
}
