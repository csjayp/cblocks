CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I $(PREFIX)/include
TARGETS	= prisond
OBJ	= main.o sock_ipc.o dispatch.o termbuf.o build.o
LIBS	= -lpthread -lutil -lprison -lcrypto
PREFIX	?= /usr/local

all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

prisond: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L $(PREFIX)/lib

clean:
	rm -fr *.o prisond *.core *.plist

install:
	[ -d $(PREFIX)/bin ] || mkdir -p $(PREFIX)/bin
	install -m 0555 prisond $(PREFIX)/bin
