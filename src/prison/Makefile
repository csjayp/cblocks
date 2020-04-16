CC	?= cc
CFLAGS	= -Wall -fsanitize=address -fstack-protector -g -I../libprison
TARGETS	= prison
LIBS	= -lprison
OBJ	= console.o launch.o y.tab.o lex.yy.o main.o sock_ipc.o

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
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS) -L../libprison

clean:
	rm -fr *.o prison *.out  y.tab.c y.tab.h y.output
