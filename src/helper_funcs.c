#include <stdio.h> // stderr, stdin
#include <errno.h> // errno
#include <stdlib.h>
#include <signal.h> // SIGINT
#include <string.h> // strerror()
#include <sys/stat.h> // stat
#include <unistd.h> //close

#include "helper_funcs.h"

// Called with wrong arguments.
void usage(char* argv0) {
	printf("usage : %s portnumber\n", argv0);
	exit(EXIT_SUCCESS);
}

// Something unexpected happened. Report error and terminate.
void sys_exit(char* msg, int* sockfd) {
	fprintf(stderr, "[ERROR] %s\n\t%s\n", msg, strerror(errno));
	if (sockfd)
		close(*sockfd);
	exit(EXIT_FAILURE);
}

// print errormsg and raise SIGINT
void sys_raise(char* msg, int* sockfd) {
	fprintf(stderr, "[ERROR] %s\n\t%s\n", msg, strerror(errno));
	if (sockfd)
		close(*sockfd);
	raise(SIGINT);
}

// print warn msg with errno
void sys_warn(char* msg){
	fprintf(stderr, "[WARNING] %s\n\t%s\n", msg, strerror(errno));
}

// gathering filesize 
int file_size(char filepath[]){
	struct stat properties;
	stat(filepath, &properties);
	return (int)properties.st_size;
}