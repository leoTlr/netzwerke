#include <stdio.h> // stderr, stdin
#include <errno.h> // errno
#include <stdlib.h>
#include <unistd.h> // close()
#include <string.h> // strerror()
#include <sys/stat.h> // stat

// Called with wrong arguments.
void usage(char *argv0){
	printf("usage : %s portnumber\n", argv0);
	exit(EXIT_SUCCESS);
}

// verbosely try to close socket
void try_close(int sockfd, int quiet){
	if (sockfd > 0){
		if (close(sockfd) == -1){
			perror("close");
		} else {
			if (quiet > 0) return;
			printf("Socket closed successfully\n");
		}
	} else {
		if (quiet > 0) return;
		printf("No socket to close\n");
	}
}

// Something unexpected happened. Report error and terminate.
void sys_err(char *msg, int exitCode, int sockfd){
	fprintf(stderr, "%s\n\t%s\n", msg, strerror(errno));

	// close socket if existing
	close(sockfd);
	
	exit(exitCode);
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