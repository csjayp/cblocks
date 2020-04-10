CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g
TARGETS	= prisond
OBJ	= main.o sock_ipc.o
LIBS	= -lpthread -lutil

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

prisond: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

clean:
	rm -fr *.o prisond
