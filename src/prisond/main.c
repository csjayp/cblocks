/*-
 * Copyright (c) 2020 Christian S.J. Peron
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
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
	{ "tty-buffer-size",	required_argument, 0, 'T' },
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
	    " -T, --tty-buffer-size=SIZE  Store at most SIZE bytes in console\n"
	);
	exit(1);
}

int
main(int argc, char *argv [], char *env[])
{
	pthread_t thr;
	int option_index;
	char *r;
	int c;

	gcfg.global_env = env;
	gcfg.c_callback = prison_handle_request;
	gcfg.c_family = PF_UNSPEC;
	gcfg.c_port = "3333";
	gcfg.c_tty_buf_size = 5 * 4096;
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "T:46U:s:p:h", long_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'T':
			gcfg.c_tty_buf_size = strtoul(optarg, &r, 10);
			if (*r != '\0') {
				errx(1, "invalid TTY buf size: %s", optarg);
			}
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
