CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I../libprison
TARGETS	= prison
LIBS	= -lprison
OBJ	= main.o sock_ipc.o

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

prison: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L../libprison

clean:
	rm -fr *.o prison
