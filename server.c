#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <sys/sendfile.h>
#include <fcntl.h>

#include "helper_funcs.c"
#include "http_funcs.c"

#define FILE_ROOT "/var/microwww/"
#define MAX_CONNECTIONS 10
#define MAX_THREADS MAX_CONNECTIONS
#define LISTEN_BACKLOG 100 // max connection queue length (see man listen)
#define BUFSIZE 2048

typedef struct thread_args_t {
	int connfd;
	int clientnr;
	struct sockaddr_in client_addr;
	socklen_t addrlen;
} thread_args_t;

typedef struct thread_exit_args_t {
	int connfd;
	char* recvBUF;
	char* sendBUF;
} thread_exit_args_t;

// thread body and exit handler
static void connection_thread(void *);
static void thread_exithandler(void *);

// global vars needed for signalhandler
int server_sockfd = -1, client_sockfd;
volatile sig_atomic_t exit_requested = 0; // volatile sig_atomic_t to prevent concurrent access
volatile sig_atomic_t thread_counter = 0; // to keep track of # running threads
struct sigaction sa;

// let program finish normally if recieving SIGINT
void sighandler(){

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
	server_addr.sin_port        = htons((uint16_t)atoi(argv[1]));

	// struct for client conection
	memset(&client_addr, 0, addrlen);
	client_addr.sin_family 	= AF_INET;
	client_addr.sin_port	= htons((uint16_t)atoi(argv[1]));

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

	printf("Waiting for incoming connections...\n");
	while (!exit_requested) {

		// wait with accepting connections if too many active
		if (thread_counter >= MAX_THREADS){
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
		const thread_args_t th_args = {client_sockfd, client_id_counter++, client_addr, addrlen};
		pthread_create(tidBUF, NULL, (void *) &connection_thread, (void *) &th_args);
		thread_counter++;		
	}

	// cleanup -----------------------------------------------
	printf("Shutting down... ");
	close(server_sockfd);
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

	// *th_args will get out of scope when new connection arrives in main
	// -> make local copy of args
	thread_args_t* args_ptr = (thread_args_t*) th_args;
	thread_args_t args;
	args.connfd = args_ptr->connfd;
	args.clientnr = args_ptr->clientnr;
	args.client_addr = args_ptr->client_addr;
	args.addrlen = args_ptr->addrlen;

	// initialize buffers and variables needed (big buffers on heap to prevent stack overflow)
	char* recvBUF = calloc(BUFSIZE, sizeof(char));
	char* sendBUF = calloc(BUFSIZE, sizeof(char));
	int msglen = 0, fileLEN = 0, fd;
	off_t offset = 0;

	char* lineBUF, *saveptr1, *saveptr2; // saveptrs needed for strtok_r;
	const char delimeter[3] = "\r\n"; // each line of request ends with carriage return + line feed
	
	char* pathptr; // path in request
	char filepath[128];
	int request_flags = 0; // flags set during check_http_request()

	// setup exit-handler
	thread_exit_args_t exit_args = {args.connfd, recvBUF, sendBUF};
	pthread_cleanup_push((void *) &thread_exithandler, (void *) &exit_args);

	printf("Connection accepted from: %s (client %d)\n", inet_ntoa(args.client_addr.sin_addr), args.clientnr);
	while (!exit_requested) {
		
		// use MSG_DONTWAIT to prevent blocking on this call (sets EAGAIN or EWOULDBLOCK if no data)
		msglen = recvfrom(args.connfd, recvBUF, BUFSIZE-1, MSG_DONTWAIT, (struct sockaddr *) &args.client_addr, &args.addrlen);
		if (msglen < 0){
			if (errno == EAGAIN || errno == EWOULDBLOCK){
				// in case of non-blocking socket and no data arrived -> sleep and loop again
				// this is needed so that the thread checks exit_requested without blocking on recvfrom
				usleep(200);
				continue;
			} else if (errno == 104) {
				printf("client %d (%s): reset connection\n", args.clientnr, inet_ntoa(args.client_addr.sin_addr));
				break;
			}
			else {
				sys_warn("Server Fault : RECVFROM");
				pthread_exit((void *) pthread_self());
			}
		}

		if (msglen == 0){
			printf("client %d (%s): closed connection\n", args.clientnr, inet_ntoa(args.client_addr.sin_addr));
			break;
		}

		// parse first line of request
		// strtok_r because we parse whole lines and tokenize again inside each line -> operating on same buffer
		lineBUF = strtok_r(recvBUF, delimeter, &saveptr1);
		request_flags = check_http_request(lineBUF, &pathptr, &saveptr2);

		// print sth like: "client 1: GET /requested-path"
		print_client_msgtype(request_flags, pathptr, args.clientnr, inet_ntoa(args.client_addr.sin_addr));

		// if no valid http request drop packet buffer, send "400-Bad request" and continue;
		if (request_flags < 1 || request_flags & INVALID_REQUEST) {
			send_400(args.connfd, sendBUF, sizeof(sendBUF));
			continue;
		}

		// POST-request not supported, send "501, not implemented"
		if (request_flags & HTTP_POST) {
			send_501(args.connfd, sendBUF, sizeof(sendBUF));
			continue;
		}

		// react on GET
		if ((request_flags & HTTP_GET) && !(request_flags & EMPTY_PATH)) {
			// add /var/microwww/ to the path
			snprintf(filepath, sizeof(filepath), "%s%s", FILE_ROOT, pathptr);
			// check if file exists/ can be read, if not send 404 
			fd = open(filepath, O_RDONLY);
			if (fd < 0) {
				send_404(args.connfd, sendBUF, sizeof(sendBUF));
				continue;
			}
			else {
				// gathering filesize
				fileLEN = file_size(filepath);
				// sending OK 
				send_200(args.connfd, fileLEN, sendBUF, sizeof(sendBUF));
				// note: sendfile is not in a posix standart and only works on linux. programm is not portable 
				 if ((sendfile(args.connfd, fd , &offset, fileLEN)) < 0) {
				 	sys_err("Server Fault: SENDFILE", -5, server_sockfd);
				 }
			}
			// close opened file
			if ((close(fd)) < 0) {
				sys_err("SERVER Fault: CLOSE", -6, server_sockfd);
			}

		}
		
		/* not implemented atm
		while (1){ // parse the other lines until (and not including) line of only \r\n
			memset(lineBUF, 0, strlen(lineBUF)+1);
			if ((lineBUF = strtok_r(NULL, delimeter, &saveptr1)) == NULL) break;
			//printf("%s\n", lineBUF);
		} */

		// send 501 not implemented as response
		//send_501(args->connfd);

		// reset buffers and continue
		memset(recvBUF, 0, BUFSIZE); 
		memset(sendBUF, 0, BUFSIZE);
	}

	// remove exit_handler (and run it)
	// exit handler frees buffers and closes socket
	pthread_cleanup_pop(1);
	pthread_exit((void *)pthread_self());
}

// close socket, free buffers, decrement thread_counter and detach thread
static void thread_exithandler(void * exit_args) {
	thread_exit_args_t *args = (thread_exit_args_t *) exit_args;

	// close socket
	if (close(args->connfd) < 0){
		char buf[50];
		snprintf(buf, sizeof(buf), "close (tid %ld)", pthread_self());
		sys_warn(buf);
	}

	// free buffers
	free(args->recvBUF);
	free(args->sendBUF);

	thread_counter--;

	// detach self so thread does not have to be joined to prevent mem leakage
	pthread_detach(pthread_self()); 
}
