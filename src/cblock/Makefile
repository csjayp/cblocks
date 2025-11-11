CC	?= cc
CFLAGS	= -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 -fno-omit-frame-pointer -fsanitize=address -fstack-protector -g -I $(PREFIX)/include -I../include
TARGETS	= cblock
LIBS	= -lcblock -lpthread -lbsm
OBJ	= build.o console.o launch.o y.tab.o lex.yy.o main.o sock_ipc.o instance.o network.o image.o
PREFIX	?= /usr/local
all:	$(TARGETS)

.c.o:
	$(CC) $(CFLAGS) -c $<

y.tab.o: grammar.y
	yacc -vd grammar.y
	$(CC) $(CFLAGS) -c y.tab.c

y.tab.h: y.tab.o

lex.yy.o: y.tab.h token.l
	lex token.l
	$(CC) $(CFLAGS) -c lex.yy.c

cblock:	$(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L $(PREFIX)/lib -L../libcblock

clean:
	rm -fr *.o $(TARGETS) *.out y.tab.c y.tab.h y.output lex.yy.c

install:
	[ -d $(PREFIX)/bin ] || mkdir -p $(PREFIX)/bin
	install -m 0555 $(TARGETS) $(PREFIX)/bin
