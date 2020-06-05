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

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <err.h>
#include <fcntl.h>
#include <unistd.h>

#include <libcblock.h>

#include "main.h"

struct network_config {
	char		*n_name;
	char		*n_netif;
	char		*n_type;
	int		 n_create;
};

static struct option network_options[] = {
	{ "create",		no_argument, 0, 'c' },
	{ "name",		required_argument, 0, 'n' },
	{ "interface",		required_argument, 0, 'i' },
	{ "type",		required_argument, 0, 't' },
	{ "help",		no_argument, 0, 'h' },
	{ 0, 0, 0, 0 }
};

static void
network_usage(void)
{
	(void) fprintf(stderr,
	    " -c, --create            Create new network\n"
	    " -n, --name=NAME         Name for network configuration\n"
	    " -i, --interface=NETIF   Network interface for inbound/outbound traffic\n"
	    " -t, --type=TYPE         Type, either 'nat' or 'bridge'\n"
	    " -h, --help              Print help\n");
	exit(1);
}

static int
network_create(int ctlsock, struct network_config *nc)
{
	struct prison_generic_command arg;
	char *payload;
	uint32_t cmd;
	vec_t *vec;

	if (nc->n_netif == NULL) {
		errx(1, "Must specify root network interface --interface");
	}
	if (nc->n_name == NULL) {
		errx(1, "Must specify name for this network --name");
	}
	vec = vec_init(32);
	cmd = PRISON_IPC_GENERIC_COMMAND;
	snprintf(arg.p_cmdname, sizeof(arg.p_cmdname),
	    "network-create");
	vec_append(vec, "-t");
	vec_append(vec, nc->n_type);
	vec_append(vec, "-n");
	vec_append(vec, nc->n_name);
	vec_append(vec, "-i");
	vec_append(vec, nc->n_netif);
	vec_finalize(vec);
	payload = vec_marshal(vec);
	if (payload == NULL) {
		err(1, "failed to marshal data");
	}
	arg.p_mlen = vec->vec_marshalled_len;
	sock_ipc_must_write(ctlsock, &cmd, sizeof(cmd));
	sock_ipc_must_write(ctlsock, &arg, sizeof(arg));
	sock_ipc_must_write(ctlsock, payload, arg.p_mlen);
	sock_ipc_from_sock_to_tty(ctlsock);
	return (0);
}

int
network_main(int argc, char *argv [], int ctlsock)
{
	struct network_config nc;
	int option_index, c;

	bzero(&nc, sizeof(nc));
	reset_getopt_state();
	nc.n_type = "bridge";
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "cn:i:t:h", network_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'c':
			nc.n_create = 1;
			break;
		case 'n':
			nc.n_name = optarg;
			break;
		case 'i':
			nc.n_netif = optarg;
			break;
		case 't':
			nc.n_type = optarg;
			break;
		case 'h':
			network_usage();
			exit(1);
		default:
			network_usage();
			/* NOT REACHED */
		}
	}
	if (nc.n_create) {
		network_create(ctlsock, &nc);
	}
	return (0);
}
