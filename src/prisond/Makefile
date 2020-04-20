CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I $(PREFIX)/include
TARGETS	= prisond
OBJ	= main.o sock_ipc.o dispatch.o termbuf.o
LIBS	= -lpthread -lutil -lprison
PREFIX	?= /usr/local

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

prisond: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L $(PREFIX)/lib

clean:
	rm -fr *.o prisond *.core *.plist
