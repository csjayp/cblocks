CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I../libprison
TARGETS	= prisond
OBJ	= main.o sock_ipc.o dispatch.o
LIBS	= -lpthread -lutil -lprison

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

prisond: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L../libprison

clean:
	rm -fr *.o prisond
