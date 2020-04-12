#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/ttycom.h>
#include <sys/param.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <stdio.h>
#include <termios.h>
#include <getopt.h>
#include <stdlib.h>
#include <signal.h>
#include <err.h>
#include <pthread.h>

#include <libprison.h>

#include "termbuf.h"
#include "main.h"
#include "sock_ipc.h"
#include "dispatch.h"

static struct option long_options[] = {
	{ "ipv4",		no_argument,	0, '4' },
	{ "ipv6",		no_argument,	0, '6' },
	{ "unix-sock",		required_argument, 0, 'U' },
	{ "listen-host",	required_argument, 0, 's' },
	{ "listen-port",	required_argument, 0, 'p' },
	{ "help",		no_argument, 0, 'h' },
	{ 0, 0, 0, 0 }
};

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Options\n"
	    " -4, --ipv4                  IPv4 sockets only\n"
	    " -6, --ipv6                  IPv6 sockets only\n"
	    " -U, --unix-sock=PATH        Path to UNIX socket\n"
	    " -s, --listen-host=HOST      Listen host/address\n"
	    " -p, --listen-port=PORT      Listen on port\n"
	);
	exit(1);
}

int
main(int argc, char *argv [], char *env[])
{
	pthread_t thr;
	int option_index;
	int c;

	gcfg.global_env = env;
	gcfg.c_callback = prison_handle_request;
	gcfg.c_family = PF_UNSPEC;
	gcfg.c_port = "3333";
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "46U:s:p:h", long_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case '4':
			gcfg.c_family = PF_INET;
			break;
		case '6':
			gcfg.c_family = PF_INET6;
			break;
		case 'U':
			gcfg.c_name = optarg;
			break;
		case 's':
			gcfg.c_host = optarg;
			break;
		case 'p':
			gcfg.c_port = optarg;
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	if (gcfg.c_family != PF_UNSPEC && gcfg.c_name) {
		errx(1, "-4, -6 and --unix-sock are incompatable");
	}
	signal(SIGPIPE, SIG_IGN);
	if (gcfg.c_name) {
		sock_ipc_setup_unix(&gcfg);
	} else {
		sock_ipc_setup_inet(&gcfg);
	}
	if (pthread_create(&thr, NULL, tty_io_queue_loop, NULL) == -1) {
		err(1, "pthread_create(tty_io_queue_loop)");
	}
	sock_ipc_event_loop(&gcfg);
	return (0);
}
