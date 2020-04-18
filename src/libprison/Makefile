CC	?= cc
CFLAGS	= -Wall -g -fstack-protector -fsanitize=address
TARGETS	= libprison.so
PREFIX	?= /usr/local

all:	$(TARGETS)

libprison.so:
	$(CC) -fPIC -shared -o libprison.so libprison.c -I.

install:
	cp libprison.so $(PREFIX)/lib
	cp libprison.h $(PREFIX)/include
	ldconfig -m

clean:
	rm -fr *.o *.so
