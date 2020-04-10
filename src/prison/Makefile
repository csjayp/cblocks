CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g
TARGETS	= prison
OBJ	= main.o

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

prison: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

clean:
	rm -fr *.o prison
