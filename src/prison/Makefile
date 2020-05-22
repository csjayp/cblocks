CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I $(PREFIX)/include
TARGETS	= prison
LIBS	= -lprison
OBJ	= build.o console.o launch.o y.tab.o lex.yy.o main.o sock_ipc.o instance.o
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

prison: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L $(PREFIX)/lib

clean:
	rm -fr *.o prison *.out  y.tab.c y.tab.h y.output lex.yy.c

install:
	cp $(TARGETS) $(PREFIX)/bin/cellblock
