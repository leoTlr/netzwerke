#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

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

// global vars needed for signalhandler
int server_sockfd = -1, client_sockfd;
volatile sig_atomic_t exit_requested = 0; // volatile sig_atomic_t to prevent concurrent access
struct sigaction sa;

// not really needed atm
CVector* started_threads;

// let program finish normally when recieving SIGINT
static void sighandler(){

	// block SIGINT during cleanup
	struct sigaction new_sa;
	new_sa.sa_handler = SIG_IGN;
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &sa);

	printf("\n"); // not signal safe but worth a try to make output look better

	// set flag for loops to stop
	// main loop and thread loops are conditioned to this flag and will stop soon
	exit_requested = 1;
	// problem: main loop or thread loops could be stuck on a blocking call
}

int main(int argc, char **argv){

	// prepare thread vector
	started_threads = malloc(sizeof(CVector));
	CVector_init(started_threads);
	if (started_threads == NULL){
		sys_err("CVector_init", 2, server_sockfd);
	}
	pthread_t* tidBUF = malloc(sizeof(pthread_t));

	// other declarations
	int client_id_counter = 1;

	// declarations for sockets
	struct sockaddr_in server_addr, client_addr; // getting casted to sockaddr on usage
	socklen_t addrlen = sizeof(struct sockaddr_in);

	// register SIGINT and SIGTERM-handler
	sigemptyset(&sa.sa_mask);
    sa.sa_handler = &sighandler;
    sa.sa_flags = 0;
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

	// set socket to be able to reuse address even if program exited abnormally
	// otherwise exiting with SIGINT can cause problems on bind at the next start of the program
	int reuse = 1;
	setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, (const char *) &reuse, sizeof(reuse));

	if (bind(server_sockfd, (struct sockaddr *) &server_addr, addrlen) == -1){
		sys_err("Server Fault : BIND", -2, server_sockfd);
	}

	if (listen(server_sockfd, LISTEN_BACKLOG) != 0){
		sys_err("Server Fault : LISTEN", -3, server_sockfd);
	}

	printf("waiting for incoming connections...\n");
	while (!exit_requested) {

		if (CVector_length(started_threads) >= MAX_THREADS){
			usleep(200);
			continue;
		}
		
		// wait for incoming TCP connection (connect() call from somewhere else)
		if ((client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_addr, &addrlen)) < 0){ // blocking call
			if (errno == 4){ // Interrupted system call
				break; // happens on SIGINT, in this case just exit the loop for cleanup
			} else {
				sys_err("Server Fault : ACCEPT", -4, server_sockfd);
			}
		}

		// prepare args and create separate thread to handle connection
		const thread_args_t th_args = {client_sockfd, client_id_counter, client_addr, addrlen};
		pthread_create(tidBUF, NULL, (void *) &connection_thread, (void *) &th_args);
		
		// append tid to started_threads vector
		CVector_append(started_threads, tidBUF);
		client_id_counter++;
	}

	// cleanup -----------------------------------------------
	close(server_sockfd);

	// wait for all threads to end normally
	for (int i = 0; i < CVector_length(started_threads); i++){
		tidBUF = CVector_get(started_threads, i);
		pthread_join(*tidBUF, NULL);
	}
	CVector_free(started_threads);
	free(tidBUF);

	// unblock SIGINT
	struct sigaction new_sa;
	new_sa.sa_handler = SIG_DFL;
	new_sa.sa_flags = 0;
	sigaction(SIGINT, &new_sa, &sa);

	printf("Cleanup finished\n");
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
	while (!exit_requested) {
		
		// use MSG_DONTWAIT to prevent blocking on this call (sets EAGAIN or EWOULDBLOCK if no data)
		msg_len = recvfrom(args->connfd, &recvBUF, BUFSIZE-1, MSG_DONTWAIT, (struct sockaddr *) &args->client_addr, &args->addrlen);
		if (msg_len < 0){
			if (errno == EAGAIN || errno == EWOULDBLOCK){
				// in case of non-blocking socket and no data arrived -> sleep and loop again
				// this is needed so that the thread checks exit_requested without blocking on recvfrom
				usleep(200);
				continue;
			} else {
				sys_warn("Server Fault : RECVFROM");
				pthread_exit((void *) pthread_self());
			}
		}
		
		if (msg_len == 0){
			printf("Closing connection to client %d (%s)\n", args->clientnr, inet_ntoa(args->client_addr.sin_addr));
			break;
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
