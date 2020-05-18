CC	?= cc
CFLAGS	= -Wall -g -fstack-protector -fsanitize=address
TARGETS	= libprison.so
OBJ	= vec.o
PREFIX	?= /usr/local

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

libprison.so: $(OBJ)
	$(CC) -fPIC -shared -o libprison.so libprison.c -I. $(OBJ)

install:
	cp libprison.so $(PREFIX)/lib
	cp libprison.h $(PREFIX)/include
	ldconfig -m

clean:
	rm -fr *.o *.so
