#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>

#include "helper_funcs.c"
#include "vector.c"

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
static void thread_exithandler(void *);

static void sighandler(int);

// global vars needed for signalhandler
CVector* started_threads;
int server_sockfd = -1, client_sockfd;
volatile sig_atomic_t exit_requested = 0;

// ensure closing socket when recieving SIGINT
static void sighandler(int signo){
	// pthread_exit should invoke the exit-handler of the threads
	// TODO only use signal-safe functions

	switch (signo){
		case SIGINT:
			printf("\nSIGINT recieved. Closing sockets\n");
			close(server_sockfd);
			for (int i=0; i < CVector_length(started_threads); i++){
				pthread_exit(CVector_get(started_threads, i));
				printf("thread closed\n");
			}
			printf("exiting\n");
			exit(EXIT_SUCCESS);
			break;
		case SIGTERM:
			printf("\nSIGTERM recieved. Closing server socket: ");
			try_close(server_sockfd);
			exit(EXIT_FAILURE);
		default:
			break;
	}
}

int main(int argc, char **argv){

	// prepare thread vector
	started_threads = malloc(sizeof(CVector));
	CVector_init(started_threads);
	if (started_threads == NULL){
		sys_err("CVector_init", 2, server_sockfd);
	}
	pthread_t tidBUF;

	// other declarations
	int client_id_counter = 1;

	// declarations for sockets
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

	// initialize TCP/IP socket
	if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1){
		sys_err("Server Fault : SOCKET", -1, server_sockfd);
	}

	// Set params so that we receive IPv4 packets from anyone on the specified port
	memset(&server_addr, 0, addrlen);
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // INADDR_ANY = 0 (ipv4 0.0.0.0)
	server_addr.sin_family      = AF_INET; // ipv4 socket
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

		if (CVector_length(started_threads) >= MAX_THREADS){
			usleep(200);
			continue;
		}
		
		// wait for incoming TCP connection (connect() call from somewhere else)
		if ((client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_addr, &addrlen)) < 0){ // blocking call
			sys_err("Server Fault : ACCEPT", -4, server_sockfd);
		}

		// prepare args and create separate thread to handle connection
		const thread_args_t th_args = {client_sockfd, client_id_counter, client_addr, addrlen};
		pthread_create(&tidBUF, NULL, (void *) &connection_thread, (void *) &th_args);
		
		// append tid to started_threads vector
		CVector_append(started_threads, &tidBUF);
		client_id_counter++;
	}

	try_close(server_sockfd);
	CVector_free(started_threads);
	return EXIT_SUCCESS;
}

static void connection_thread(void * th_args) {

	// unsure: values in th_args could get overwritten in main thread
	// make local copy of args
	thread_args_t* args = (thread_args_t*)malloc(sizeof(thread_args_t));
	memcpy(args, (thread_args_t*) th_args, sizeof(thread_args_t));

	int msg_len = 0;

	// initialize buffer
	char recvBUF[BUFSIZE];
	memset(recvBUF, 0, BUFSIZE);

	// setup exit-handler
	pthread_cleanup_push((void *) &thread_exithandler, (void *) args);

	printf("Connection accepted from: %s (client %d)\n", inet_ntoa(args->client_addr.sin_addr), args->clientnr);

	// as long as data arrives, print data
	while (!exit_requested && 
			(msg_len = recvfrom(args->connfd, &recvBUF, BUFSIZE-1, 0, (struct sockaddr *) &args->client_addr, &args->addrlen)) >= 0) {
		
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

static void thread_exithandler(void * th_args) {
	// close socket on exit
	thread_args_t *args = (thread_args_t *) th_args;
	printf("client %d: ", args->clientnr);
	try_close(args->connfd);
}
