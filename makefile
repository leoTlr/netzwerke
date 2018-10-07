CC=cc
LINK=cc
CFLAGS= -g -Wall -Wextra
LFLAGS= -o $@
LIBDIR=
LIBS=
OBJ=o
del=rm
EXE=

all : server client

server : server.$(OBJ)
	$(LINK) $(LFLAGS) server.$(OBJ) $(LIBS)

server.$(OBJ) : server.c
	$(CC) -c server.c $(CFLAGS)

client : client.$(OBJ)
	$(LINK) $(LFLAGS) client.$(OBJ) $(LIBS)

client.$(OBJ) : client.c
	$(CC) -c client.c $(CFLAGS)

clean :
	$(del) client.$(OBJ)
	$(del) server.$(OBJ)
	$(del) server$(EXE)
	$(del) client$(EXE)
