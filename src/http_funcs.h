#ifndef HTTP_FUNCS_H
#define HTTP_FUNCS_H

#include <stdlib.h>

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

/*  Parses one line token by token setting flags.
    Sets path_token_ptr to null-terminated string containing the requested path (might become NULL).
    Will set INVALID_REQUEST if path too big for buffer.
    Saveptr needed by strtok_r
    returns -1 on error else http_request flags set   */
int check_http_request(char* lineBUF, char** path_token_ptr, const size_t pathlen_max, char** saveptr);

// print message based on flags
// i.e "client 1: GET /requested/path"
void print_client_msgtype(const int request_flags, const char* path, const int client_nr, const char* addr_str);

// responses with status codes
void send_200(const int connfd, int fileLEN, char* sendBUF, const size_t buflen);
void send_400(const int connfd, char* sendBUF, const size_t buflen);
void send_404(const int connfd, char* sendBUF, const size_t buflen);
void send_501(const int connfd, char* sendBUF, const size_t buflen);

#endif // HTTP_FUNCS_H
