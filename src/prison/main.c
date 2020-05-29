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
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/un.h>
#include <sys/ttycom.h>
#include <netinet/in.h>

#include <stdio.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <err.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <libprison.h>

#include "main.h"
#include "sock_ipc.h"

struct sub_command {
	char		*sc_name;
	int		(*sc_callback)(int, char **, int);
	char		*sc_description;
};

static struct sub_command sub_command_list[] = {
	{ "launch",	launch_main, "Launch a new container instance"  },
	{ "console",	console_main, "Attach to a container console" },
	{ "build",	build_main, "Build a new container image" },
	{ "instance",	instance_main, "Get information about running instances" },
	{ "network",    network_main, "Configure networking parameters" },
	{ NULL,		NULL, NULL }
};

static struct option long_options[] = {
	{ "ipv4",		no_argument,	0, '4' },
	{ "ipv6",		no_argument,	0, '6' },
	{ "unix-sock",		required_argument, 0, 'U' },
	{ "host",		required_argument, 0, 's' },
	{ "port",		required_argument, 0, 'p' },
	{ "help",		no_argument, 0, 'h' },
	{ 0, 0, 0, 0 }
};

static void
usage(void)
{
	struct sub_command *scp;

	(void) fprintf(stderr,
	    "Usage: prison [OPTIONS] COMMAND\n\n"
	    "Options\n"
	    " -4, --ipv4                IPv4 sockets only\n"
	    " -6, --ipv6                IPv6 sockets only\n"
	    " -U, --unix-sock=PATH      Path to UNIX socket\n"
	    " -s, --host=HOST           Connect to host/address\n"
	    " -p, --port=PORT           Conect to port\n"
	    " -h, --help                Display program usage\n\n"
	    "Commands:\n");
	for (scp = sub_command_list; scp->sc_name != NULL; scp++) {
		(void) fprintf(stderr,
		    " %-10s %s\n", scp->sc_name, scp->sc_description);
	}
	(void) fprintf(stderr,
	    "\nRun 'prison COMMAND --help' for info on a command.\n");
	exit(1);
}

static int
sub_command_match(const char *input)
{
	struct sub_command *scp;

	for (scp = sub_command_list; scp->sc_name != NULL; scp++) {
		if (strcmp(scp->sc_name, input) == 0) {
			return (1);
		}
	}
	return (0);
}

static int
locate_sub_command(int argc, char *argv[])
{
	int k;

	for (k = 0; k < argc; k++) {
		if (sub_command_match(argv[k])) {
			return (k);
		}
	}
	return (-1);
}

void
reset_getopt_state(void)
{

	opterr = 1;
	optind = 1;
	optopt = '?';
}

int
main(int argc, char *argv [])
{
	int option_index, c, ctlsock, sc_index, j, ret;
	struct sub_command *scp;
	char **main_argv;

	sc_index = locate_sub_command(argc, argv);
	if (sc_index == -1) {
		(void) fprintf(stderr, "ERROR: no command was specified\n");
		usage();
	}
	main_argv = calloc(sc_index + 1, sizeof(char *));
	if (main_argv == NULL) {
		err(1, "calloc failed");
	}
	for (j = 0; j < sc_index; j++) {
		main_argv[j] = argv[j];
	}
	main_argv[sc_index] = NULL;
	argv += sc_index;
	argc -= sc_index;
	gcfg.c_family = PF_UNSPEC;
	gcfg.c_port = "3333";
	while (1) {
		option_index = 0;
		c = getopt_long(sc_index, main_argv, "h46U:s:p:h", long_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'h':
			usage();
			exit(1);
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
		}
	}
	signal(SIGPIPE, SIG_IGN);
	for (scp = sub_command_list; scp->sc_name != NULL; scp++) {
		if (strcmp(scp->sc_name, argv[0]) == 0) {
			break;
		}
	}
	if (gcfg.c_name) {
		ctlsock = sock_ipc_connect_unix(&gcfg);
	} else {
		ctlsock = sock_ipc_connect_inet(&gcfg);
	}
	ret = (*scp->sc_callback)(argc, argv, ctlsock);
	return (ret);
}
