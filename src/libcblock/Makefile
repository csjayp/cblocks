CC	?= cc
CFLAGS	= -Wall -g -fstack-protector -fsanitize=address -I../include
TARGETS	= libcblock.so
OBJ	= vec.o print.o sbuf.o
PREFIX	?= /usr/local

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $< -fPIC

libcblock.so: $(OBJ)
	$(CC) -fPIC -shared -o libcblock.so libcblock.c -I. $(OBJ)

install:
	[ -d $(PREFIX)/lib ] || mkdir -p $(PREFIX)/lib
	install -m 0555 $(TARGETS) $(PREFIX)/lib
	ldconfig -m

clean:
	rm -fr *.o *.so
