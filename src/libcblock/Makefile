CC	?= cc
CFLAGS	= -Wall -fno-omit-frame-pointer -g -fstack-protector -I../include
TARGETS	= libcblock.a
OBJ	= libcblock.o vec.o print.o wire.o proto.o

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

libcblock.a: $(OBJ)
	rm -f libcblock.a
	ar rcs libcblock.a $(OBJ)

clean:
	rm -fr *.o *.a *.so
