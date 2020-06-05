CC	?= cc
CFLAGS	= -Wall -g -fstack-protector -fsanitize=address
TARGETS	= libcblock.so
OBJ	= vec.o
PREFIX	?= /usr/local

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

libcblock.so: $(OBJ)
	$(CC) -fPIC -shared -o libcblock.so libcblock.c -I. $(OBJ)

install:
	cp libcblock.so $(PREFIX)/lib
	cp libcblock.h $(PREFIX)/include
	ldconfig -m

clean:
	rm -fr *.o *.so
