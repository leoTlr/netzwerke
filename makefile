# universal makefile from https://stackoverflow.com/a/28663974/9986282

appname := server

CC := gcc
CCFLAGS := -Wall -Wextra -g
LDFLAGS :=
LDLIBS := -lpthread

SRCDIR := ./src

srcext := c
srcfiles := $(shell find $(SRCDIR) -name "*.$(srcext)")
objects  := $(patsubst %.$(srcext), %.o, $(srcfiles))

all: $(appname)

$(appname): $(objects)
	$(CC) $(CCFLAGS) $(LDFLAGS) -o $(appname) $(objects) $(LDLIBS)

depend: .depend

.depend: $(srcfiles)
	rm -f ./.depend
	$(CC) $(CCFLAGS) -MM $^>>./.depend;

clean:
	rm -f $(objects) $(appname)

#dist-clean: clean
#	rm -f *~ .depend

include .depend
