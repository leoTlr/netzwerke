#include <string.h>
#include <stdio.h>
#include <arpa/inet.h>

#include "helper_funcs.c"

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
// sets path_token_ptr to null-terminated string containing the requested path (might become NULL)
// saveptr needed by strtok_r
// returns -1 on error else http_request enum with flags set
int check_http_request(char* lineBUF, char** path_token_ptr, char** saveptr){

    if (lineBUF == NULL) return -1;

    int flags = 0;

    // first token is everything before first space
    char* request_type = strtok_r(lineBUF, " ", saveptr);
    if (strcmp(request_type, "GET") == 0) flags |= HTTP_GET;
    else if (strcmp(request_type, "POST") == 0) flags |= HTTP_POST;
    else if (strcmp(request_type, "HEAD") == 0) flags |= HTTP_HEAD;
    else flags |= INVALID_REQUEST; // no valid http1.0 request

    // skip spaces and forward to / (delim space to include / in str)
    // set provided ptr to start of path
    *path_token_ptr = strtok_r(NULL, " ", saveptr);
    if (*path_token_ptr == NULL) return flags | INVALID_REQUEST;
    if (strlen(*path_token_ptr) <= 1) flags |= EMPTY_PATH;

    // version token i.e "HTTP/1.0"
    char* version = strtok_r(NULL, " ", saveptr);
    if (version == NULL) return flags | INVALID_REQUEST;
    
    if (strcmp(version, "HTTP/1.0") == 0) return flags;
    else return flags | UNSUPPORTED_VERSION;
}

// using given socket, send 501 - not implemented
void send_501(const int connfd, char* sendBUF, const size_t buflen){
    if (connfd <= 0) return;
    if (sendBUF == NULL || buflen == 0) return;

    memset(sendBUF, 0, buflen);

    const char* answer_line =       "HTTP/1.0 501 Not Implemented\r\n";
    const char* content_type =      "Content-type: text/html\r\n";
    const char* server =            "Server: MicroWWW Team 06\r\n";
    const char* content_length =    "Content-length: 63\r\n";
    const char* crlf =              "\r\n";
    const char* entity_body =       "<html><body><b>501</b> - Operation not supported</body></html>\r\n";

    strcat(sendBUF, answer_line);
    strcat(sendBUF, content_type);
    strcat(sendBUF, server);
    strcat(sendBUF, content_length);
    strcat(sendBUF, crlf);
    strcat(sendBUF, entity_body);

    if (send(connfd, sendBUF, strlen(sendBUF), 0) < 0){
        sys_warn("[WARNING] error sending message\n");
    }
}

// using given socket, send 400 Bad Request
void send_400(const int connfd, char* sendBUF, const size_t buflen){
    if (connfd <= 0) return;
    if (sendBUF == NULL || buflen == 0) return;

    memset(sendBUF, 0, buflen);

    const char* answer_line =       "HTTP/1.0 400 Bad Request\r\n";
    const char* content_type =      "Content-type: text/html\r\n";
    const char* server =            "Server: MicroWWW Team 06\r\n";
    const char* content_length =    "Content-length: 52\r\n";
    const char* crlf =              "\r\n";
    const char* entity_body =       "<html><body><b>400</b> - Bad Request </body></html>\r\n";

    strcat(sendBUF, answer_line);
    strcat(sendBUF, content_type);
    strcat(sendBUF, server);
    strcat(sendBUF, content_length);
    strcat(sendBUF, crlf);
    strcat(sendBUF, entity_body);

    if (send(connfd, sendBUF, strlen(sendBUF), 0) < 0){
        sys_warn("[WARNING] error sending message\n");
    }
}

// using given socket, send 404 Not Found 
void send_404(const int connfd, char* sendBUF, const size_t buflen) {
    if (connfd <= 0) return;
    if (sendBUF == NULL || buflen == 0) return;

    memset(sendBUF, 0, buflen);

    const char* answer_line =       "HTTP/1.0 404 Not Found\r\n";
    const char* content_type =      "Content-type: text/html\r\n";
    const char* server =            "Server: MicroWWW Team 06\r\n";
    const char* content_length =    "Content-length: 50\r\n";
    const char* crlf =              "\r\n";
    const char* entity_body =       "<html><body><b>404</b> - Not Found </body></html>\r\n";

    strcat(sendBUF, answer_line);
    strcat(sendBUF, content_type);
    strcat(sendBUF, server);
    strcat(sendBUF, content_length);
    strcat(sendBUF, crlf);
    strcat(sendBUF, entity_body);

    if (send(connfd, sendBUF, strlen(sendBUF), 0) < 0){
        sys_warn("[WARNING] error sending message\n");
    }
}

// using given socket, send 200 OK & file length
void send_200(const int connfd, int fileLEN) {
    if (connfd <= 0) return;
    
    // file length is needed for correct data transfer
    char fileBUF[128];
    snprintf(fileBUF, sizeof(fileBUF), "%i%s", fileLEN, "\r\n");

    char msg[1024] =                "HTTP/1.0 200 OK\r\n";
    const char* content_type =      "Content-type: text/html\r\n";
    const char* content_length =    "Content-length:";
    const char* server =            "Server: MicroWWW Team 06\r\n";
    const char* crlf =              "\r\n";

    strcat(msg, content_type);
    strcat(msg, content_length);
    strcat(msg, fileBUF);
    strcat(msg, server);
    strcat(msg, crlf);

    if (send(connfd, msg, strlen(msg), 0) < 0){
        sys_warn("[WARNING] error sending message\n");
    }
}

// print message based on flags
// i.e "client 1: GET /requested/path"
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

