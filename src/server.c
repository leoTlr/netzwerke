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
#include <sys/sem.h>
#include <errno.h>

#include "helper_funcs.h"
#include "http_funcs.h"
#include "tidstack.h"

#define FILE_ROOT "/var/microwww/"
#define FILEPATH_BUF 256
#define MAX_REQUEST_PATHLEN (FILEPATH_BUF-strlen(FILE_ROOT)-1)
#define MAX_CONNECTIONS 10
#define MAX_THREADS MAX_CONNECTIONS
#define LISTEN_BACKLOG 100 // max connection queue length (see man listen)
#define BUFSIZE 2048
#define LOCK 1 // for semaphore
#define UNLOCK -1

typedef struct {
	int connfd;
	int clientnr;
	struct sockaddr_in client_addr;
	socklen_t addrlen;
} thread_args_t;

typedef struct {
	int connfd;
	char* recvBUF;
	char* sendBUF;
	char* filepathBUF;
} thread_exit_args_t;

// thread body and exit handler
static void connection_thread(void *);
static void thread_exithandler(void *);

// lock/unlock semaphore
int sem_operation();

// WARNING: not thread-safe -> only use in main thread (or protect with semaphore)
// global vars needed for semaphore or needed inside threads and main
int server_sockfd = -1, client_sockfd;
tidstack_t join_stack; // store thread id's to be able to join them (NOT thread safe)
// volatile sig_atomic_t is safe to use in sighandlers but is NOT thread-safe (on write)
volatile sig_atomic_t exit_requested = 0; // only reads are thread-safe
volatile int thread_counter = 0; // WARNING always use mutex on writes to be thread-safe
// mutex for counter as it always will be the same thread to lock and unlock
static pthread_mutex_t threadcount_mutex = PTHREAD_MUTEX_INITIALIZER;
// semaphore because it gets locked and unlocked in different threads
static int copysem_id; 

// let program finish normally if recieving SIGINT or SIGTERM
void sighandler(int signo){

	// block SIGINT and SIGTERM during cleanup
	struct sigaction sa = {0};
	sigemptyset(&sa.sa_mask);
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	// not signal safe but worth a try for logging reasons
	if (signo == SIGINT)
		printf("SIGINT recieved, exiting\n");
	else if (signo == SIGTERM)
		printf("SIGTERM recieved, exiting\n");
	else
		printf("Signal (%d) recieved, exiting\n", signo);

	// set flag for loops to stop
	// main loop and thread loops are conditioned to this flag and will stop soon
	exit_requested = 1;
	// warning: main loop or thread loops could be stuck on a blocking call
	// -> dont use blocking calls
}

int main(int argc, char **argv){

	// Check for right number of arguments
	if (argc != 2) usage(argv[0]);

	struct sockaddr_in server_addr, client_addr;
	socklen_t addrlen = sizeof(struct sockaddr_in);
	int client_id_counter = 1;
	pthread_t tid;

	tidstack_init(&join_stack);

	// register SIGINT-handler
	struct sigaction sa = {0};
	sigemptyset(&sa.sa_mask);
    sa.sa_handler = &sighandler;
    sa.sa_flags = 0;
    if ((sigaction(SIGINT, &sa, NULL)) < 0)
		sys_exit("Could not register SIGINT-handler", NULL);
	if ((sigaction(SIGTERM, &sa, NULL)) < 0)
		sys_exit("Could not register SIGTERM-handler", NULL);

	// semaphore to prevent arguments for threads getting out of scope before thread made a local copy
    if ((copysem_id=semget(IPC_PRIVATE, 1, 0660)) < 0)
        sys_exit("Could not register semaphore", NULL);
    else
        // init with 1 (unlocked)
        if (semctl(copysem_id, 0, SETVAL, 1) < 0)
            sys_exit("Could not initialize semaphore", NULL);

	// initialize TCP/IP socket (nonblocking to prevent waiting for accept() when recieving SIGINT)
	if ((server_sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)) == -1)
		sys_exit("Server Fault : SOCKET", &server_sockfd);

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

	if (bind(server_sockfd, (struct sockaddr *) &server_addr, addrlen) == -1)
		sys_exit("Server Fault : BIND", &server_sockfd);

	if (listen(server_sockfd, LISTEN_BACKLOG) != 0)
		sys_exit("Server Fault : LISTEN", &server_sockfd);

	printf("Waiting for incoming connections...\n");
	while (!exit_requested) {

		// wait with accepting connections if too many active
		if (thread_counter >= MAX_THREADS){
			usleep(100);
			continue;
		}
		
		// wait for incoming TCP connection (connect() call from somewhere else)
		if ((client_sockfd = accept(server_sockfd, (struct sockaddr *) &client_addr, &addrlen)) < 0){
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				// happens if no connection ready because socket is nonblocking
				usleep(10);
				continue;
			} else {
				// raise SIGINT calling the handler letting threads end gracefully
				sys_raise("Server Fault : ACCEPT", &server_sockfd);
				break;
			}
		}

		/* 	prepare args and create separate thread to handle connection
		 	lock semaphore to make sure the thread can create a local copy of his args and can safely push his tid on tidstack
			(will get unlocked inside thread)	*/
		if (sem_operation(LOCK) < 0) { // possibly blocking call
			sys_raise("Server Fault : sem_operation", &server_sockfd);
			break;
		} 

		const thread_args_t th_args = {client_sockfd, client_id_counter++, client_addr, addrlen};
		pthread_create(&tid, NULL, (const void *) &connection_thread, (void *) &th_args);

		// safely increment thread counter
		if (pthread_mutex_lock(&threadcount_mutex) != 0) {
			sys_raise("mainthread error locking counter mutex", &server_sockfd);
		} else {
			thread_counter++;
			pthread_mutex_unlock(&threadcount_mutex);
		}
	}

	// cleanup -----------------------------------------------
	printf("Shutting down... ");
	close(server_sockfd);

	// wait for all threads to finish
	// no need to use a semaphore/mutex here because tidstack_pop() only gets called in main thread
	while (1) {
		tid = tidstack_pop(&join_stack);
		if (tid == 0)
			break;
		pthread_join(tid, NULL);
	}

	tidstack_destroy(&join_stack);
	if (semctl(copysem_id, 0, IPC_RMID) < 0)
		sys_warn("Could not delete Semaphore ");

	printf("Cleanup finished\n");
	pthread_exit(NULL);
}

void connection_thread(void * th_args) {

	// *th_args will get out of scope when new connection arrives in main
	// -> make local copy of args and unlock the semaphore locked in main loop
	thread_args_t args = *((thread_args_t*) th_args);

	// still with locked semaphore
	tidstack_push(&join_stack, pthread_self());

	if (sem_operation(UNLOCK) < 0) {
		// not possible for program to continue running if unlocking failed
		// raise SIGINT to let other threads end gracefully and end self
		sys_raise("Server Fault : sem_operation", NULL);
		pthread_exit((void*)pthread_self());
	}

	// initialize buffers and variables needed (big buffers on heap to prevent stack overflow)
	char* recvBUF = calloc(BUFSIZE, sizeof(char));
	char* sendBUF = calloc(BUFSIZE, sizeof(char));
	char* filepathBUF = calloc(FILEPATH_BUF, sizeof(char));
	int msglen = 0, fileLEN = 0, fd;
	off_t offset = 0;

	char* lineBUF, *saveptr1, *saveptr2; // saveptrs needed for strtok_r;
	const char delimeter[3] = "\r\n"; // each line of request ends with carriage return + line feed
	
	char* pathptr; // path in request
	int request_flags = 0; // flags set during check_http_request()

	// setup exit-handler
	thread_exit_args_t exit_args = {args.connfd, recvBUF, sendBUF, filepathBUF};
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
			} else if (errno == ECONNRESET) {
				printf("client %d (%s): reset connection\n", args.clientnr, inet_ntoa(args.client_addr.sin_addr));
				break;
			}
			else {
				sys_raise("Server Fault : RECVFROM", NULL);
				pthread_exit((void *) pthread_self());
			}
		}

		if (msglen == 0){ // in case of implementing keep alive http option, dont break here and continue
			printf("client %d (%s): closed connection\n", args.clientnr, inet_ntoa(args.client_addr.sin_addr));
			break;
		}

		// parse first line of request
		// strtok_r because we parse whole lines and tokenize again inside each line -> operating on same buffer
		lineBUF = strtok_r(recvBUF, delimeter, &saveptr1);
		request_flags = check_http_request(lineBUF, &pathptr, MAX_REQUEST_PATHLEN, &saveptr2);

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
			strcat(filepathBUF, FILE_ROOT);
			strcat(filepathBUF, pathptr);
			// check if file exists/ can be read, if not send 404 
			fd = open(filepathBUF, O_RDONLY);
			if (fd < 0) {
				send_404(args.connfd, sendBUF, sizeof(sendBUF));
				continue;
			}
			else {
				// gathering filesize
				fileLEN = file_size(filepathBUF);
				// sending OK 
				send_200(args.connfd, fileLEN, sendBUF, sizeof(sendBUF));
				// note: sendfile is not in a posix standart and only works on linux. programm is not portable 
				 if ((sendfile(args.connfd, fd , &offset, fileLEN)) < 0) 
				 	sys_warn("Server Fault: SENDFILE");
				
				// close opened file
				if ((close(fd)) < 0) 
					sys_warn("Server Fault: CLOSE");
			}

		}
		
		/* not implemented atm
		while (1){ // parse the other lines until (and not including) line of only \r\n
			memset(lineBUF, 0, strlen(lineBUF)+1);
			if ((lineBUF = strtok_r(NULL, delimeter, &saveptr1)) == NULL) break;
			//printf("%s\n", lineBUF);
		} */

		// reset buffers and continue
		memset(recvBUF, 0, BUFSIZE); 
		memset(sendBUF, 0, BUFSIZE);
		memset(filepathBUF, 0, FILEPATH_BUF);
	}

	// remove exit_handler (and run it)
	// exit handler frees buffers and closes socket
	pthread_cleanup_pop(1);
	pthread_exit((void *)pthread_self());
}

// close socket, free buffers, decrement thread_counter and detach thread
static void thread_exithandler(void * exit_args) {
	thread_exit_args_t args = *((thread_exit_args_t *) exit_args);

	// close socket
	if (close(args.connfd) < 0)
		sys_warn("thread_exithandler : close");

	// free buffers
	free(args.recvBUF);
	free(args.sendBUF);
	free(args.filepathBUF);

	// safely decrement thread counter
	if (pthread_mutex_lock(&threadcount_mutex) != 0) {
		sys_raise("thread_exithandler error locking counter mutex", NULL);
	} else {
		thread_counter--;
		pthread_mutex_unlock(&threadcount_mutex);
	}
}

// helper-function for locking/unlocking the semaphre, return 0 on success or -1 if failure
int sem_operation(int op) {

    static struct sembuf sbuf;
    sbuf.sem_op = op;
    sbuf.sem_flg = SEM_UNDO;

    if (semop(copysem_id, &sbuf, 1) < 0)
        return -1;

    return 0;
}
