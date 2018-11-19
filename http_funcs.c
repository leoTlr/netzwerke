#include <string.h>
#include <stdio.h>

enum request_flags {
    INVALID_REQUEST = 1,
    UNSUPPORTED_VERSION = 2,
    HTTP_GET = 4,
    HTTP_POST = 8,
    HTTP_HEAD = 16,
    EMPTY_PATH = 32,
} request_flag ;

enum http_status_codes {
    OK = 200,
    CREATED = 201,
    ACCEPTED = 202,
    NO_CONTENT = 204,
    MOVED_PERMANENTLY = 301,
    MOVED_TEMPORARILY = 302,
    NOT_MODIFIED = 304,
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503,
} http_status_code ;

// parses one line token by token setting flags
// writes requested path into pathBUF
// saveptr needed by strtok_r
// returns -1 on error else http_request enum with flags set
int check_http_request(char* lineBUF, char** pathBUF, size_t pathBUFsize, char** saveptr){

    if (lineBUF == NULL) return -1;
    if (*pathBUF == NULL) return -1;

    int flags = 0;
    size_t pathlen;

    // first token is everything before first space
    char* request_type = strtok_r(lineBUF, " ", saveptr);
    if (strcmp(request_type, "GET") == 0) flags |= HTTP_GET;
    else if (strcmp(request_type, "POST") == 0) flags |= HTTP_POST;
    else if (strcmp(request_type, "HEAD") == 0) flags |= HTTP_HEAD;
    else flags |= INVALID_REQUEST; // no valid http1.0 request

    // skip spaces and forward to / (delim space to include / in str)
    char* path = strtok_r(NULL, " ", saveptr);
    if (path == NULL) return flags | INVALID_REQUEST;

    pathlen = strlen(path);
    if (pathlen <= 1) flags |= EMPTY_PATH;
    else if (pathlen+1 > pathBUFsize){
        // prevent buffer overflow
        printf("[WARNING] path buffer too small for requested path\n");
        return -1;
    }
    *pathBUF = path; // write path in provided buffer

    // version token i.e "HTTP/1.0"
    char* version = strtok_r(NULL, " ", saveptr);
    if (version == NULL) return flags | INVALID_REQUEST;
    if (strcmp(version, "HTTP/1.0") == 0) return flags;
    else return flags | UNSUPPORTED_VERSION;
}

void print_client_msgtype(const int request_flags, const char* path, const int client_nr, const char* addr_str){
    if (request_flags < 0){
		printf("client %d (%s): error parsing request\n", client_nr, addr_str);
    }
    else if (request_flags & INVALID_REQUEST){
		printf("client %d (%s): bad request\n", client_nr, addr_str);
	}
    else if (request_flags & HTTP_GET){
		printf("client %d (%s): GET %s\n", client_nr, addr_str, path);
    }
    else if (request_flags & HTTP_POST){
        printf("client %d (%s): POST %s\n", client_nr, addr_str, path);
    }
    else if (request_flags & HTTP_HEAD){
        printf("client %d (%s): HEAD %s\n", client_nr, addr_str, path);
    }
}