CC=cc
LINK=cc
CFLAGS= -g -Wall -Wextra
LFLAGS= -o $@
LIBDIR=
LIBS=-lpthread
OBJ=o
del=rm
EXE=

all : server

server : server.$(OBJ) http_funcs.$(OBJ) helper_funcs.$(OBJ)
	$(LINK) $(LFLAGS) server.$(OBJ) $(LIBS)

server.$(OBJ) : server.c http_funcs.c helper_funcs.c
	$(CC) -c server.c $(CFLAGS)

http_funcs.$(OBJ) :
	$(CC) -c http_funcs.c $(CFLAGS)

helper_funcs.$(OBJ) :
	$(CC) -c helper_funcs.c $(CFLAGS)

clean :
	$(del) server.$(OBJ)
	$(del) server$(EXE)
