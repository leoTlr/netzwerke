#ifndef HELPER_FUNCS_H
#define HELPER_FUNCS_H

// print usage
void usage(char *argv0);

// Something unexpected happened. Report error and terminate.
void sys_exit(char* msg, int* sockfd);

// print errormsg and raise SIGINT
void sys_raise(char* msg, int* sockfd);

// print warn msg with errno
void sys_warn(char* msg);

// gathering filesize 
int file_size(char filepath[]);

#endif // HELPER_FUNCS_H