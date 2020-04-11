CC	?= cc
CFLAGS	= -Wall -g -fstack-protector -fsanitize=address
TARGETS	= libprison.so

all:	$(TARGETS)

libprison.so:
	$(CC) -fPIC -shared -o libprison.so libprison.c -I.

clean:
	rm -fr *.o *.so
