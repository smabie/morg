# Makefile

# 
# This file is public domain as declared by Sturm Mabie.
# 

CC=gcc
PROG=morg
FILES=morg.c basename.c dirname.c
CFLAGS+=-Wall -ansi -pedantic -D_BSD_SOURCE
CPPFLAGS+=-I/usr/local/include
LDFLAGS+=-L/usr/local/lib -ltag -ltag_c
ifeq ($(shell uname),Linux)
	CFLAGS+=-lbsd
endif
all:
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LDFLAGS) $(FILES) -o $(PROG)
clean:
	rm $(PROG)

