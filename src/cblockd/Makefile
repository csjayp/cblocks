#
CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I $(PREFIX)/include -I../include/
TARGETS	= cblockd
OBJ	= main.o sock_ipc.o dispatch.o termbuf.o build.o instances.o exec.o tty.o util.o cblock.o
LIBS	= -lpthread -lutil -lcblock -lcrypto
PREFIX	?= /usr/local

all:	$(TARGETS)

.c.o:	probes.h
	$(CC) $(CFLAGS) -c $<

probes.o: $(OBJ)
	dtrace -G -s probes.d -o probes.o -64 $(OBJ)

probes.h: probes.d
	dtrace -o probes.h -h -s probes.d

cblockd: $(OBJ) probes.o
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L $(PREFIX)/lib -L../libcblock probes.o

clean:
	rm -fr *.o $(TARGETS) *.core *.plist *.outa probes.h

install:
	[ -d $(PREFIX)/bin ] || mkdir -p $(PREFIX)/bin
	install -m 0555 $(TARGETS) $(PREFIX)/bin
