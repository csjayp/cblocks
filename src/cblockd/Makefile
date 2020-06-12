CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I $(PREFIX)/include -I../include
TARGETS	= cblockd
OBJ	= main.o sock_ipc.o dispatch.o termbuf.o build.o instances.o exec.o
LIBS	= -lpthread -lutil -lcblock -lcrypto
PREFIX	?= /usr/local

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

cblockd: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L $(PREFIX)/lib -L../libcblock

clean:
	rm -fr *.o $(TARGETS) *.core *.plist

install:
	[ -d $(PREFIX)/bin ] || mkdir -p $(PREFIX)/bin
	install -m 0555 $(TARGETS) $(PREFIX)/bin
