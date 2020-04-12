#include <stdio.h>
#include <time.h>

int
main(int argc, char *argv [])
{
	for (;;) {
		printf("%u timestamp\n", time(NULL));
		sleep(1);
	}
	return (0);
}
