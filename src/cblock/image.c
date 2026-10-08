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
#include <time.h>
#include <unistd.h>

#include <cblock/libcblock.h>

#include "main.h"

struct image_config {
	int		 i_quiet;
	int		 i_do_prune;
	int		 i_do_remove;
};

static struct option image_options[] = {
	{ "help",		no_argument, 0, 'h' },
	{ "quiet",		no_argument, 0, 'q' },
	{ "prune",		no_argument, 0, 'p' },
	{ "remove",		no_argument, 0, 'r' },
	{ 0, 0, 0, 0 }
};

static void
image_usage(void)
{
	(void) fprintf(stderr,
	    "Usage: cblock images [OPTIONS]\n"
	    "       cblock images --remove NAME[:TAG] ...\n\n"
	    "Options\n"
	    " -h, --help                  Print help\n"
	    " -p, --prune                 Remove stopped/dead images\n"
	    " -r, --remove                Remove image tags, and each image\n"
	    "                             with its last tag unless it is in use\n"
	    " -q, --quiet                 Do not print column headers\n");
	exit(1);
}

static void
image_prune(struct image_config *icp __attribute__((unused)), int ctlsock)
{
	struct cblock_generic_command arg;

	bzero(&arg, sizeof(arg));
	snprintf(arg.p_cmdname, sizeof(arg.p_cmdname), "image_prune");
	if (sock_ipc_write_u32(ctlsock, PRISON_IPC_GENERIC_COMMAND) == -1 ||
	    proto_send_generic_command(ctlsock, &arg, NULL) == -1) {
		errx(1, "failed to send image request");
	}
	sock_ipc_from_sock_to_tty(ctlsock);
}

static void
image_remove(int argc, char *argv[], int ctlsock)
{
	struct cblock_generic_command arg;
	char *marshalled;
	vec_t *vec;
	int k;

	vec = vec_init(argc + 1);
	if (vec == NULL) {
		err(1, "vec_init failed");
	}
	for (k = 0; k < argc; k++) {
		vec_append(vec, argv[k]);
	}
	vec_finalize(vec);
	marshalled = vec_marshal(vec);
	if (marshalled == NULL) {
		err(1, "vec_marshal failed");
	}
	bzero(&arg, sizeof(arg));
	snprintf(arg.p_cmdname, sizeof(arg.p_cmdname), "image_remove");
	arg.p_mlen = vec->vec_marshalled_len;
	if (sock_ipc_write_u32(ctlsock, PRISON_IPC_GENERIC_COMMAND) == -1 ||
	    proto_send_generic_command(ctlsock, &arg, marshalled) == -1) {
		errx(1, "failed to send image request");
	}
	vec_free(vec);
	sock_ipc_from_sock_to_tty(ctlsock);
}

static void
image_get(struct image_config *icp __attribute__((unused)), int ctlsock)
{
	struct cblock_generic_command arg;

	bzero(&arg, sizeof(arg));
	snprintf(arg.p_cmdname, sizeof(arg.p_cmdname), "image_list");
	if (sock_ipc_write_u32(ctlsock, PRISON_IPC_GENERIC_COMMAND) == -1 ||
	    proto_send_generic_command(ctlsock, &arg, NULL) == -1) {
		errx(1, "failed to send image request");
	}
	sock_ipc_from_sock_to_tty(ctlsock);
}

int
image_main(int argc, char *argv [], int ctlsock)
{
	struct image_config ic;
	int option_index, c;

	bzero(&ic, sizeof(ic));
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "qhpr", image_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'p':
			ic.i_do_prune = 1;
			break;
		case 'r':
			ic.i_do_remove = 1;
			break;
		case 'q':
			ic.i_quiet = 1;
			break;
		case 'h':
			image_usage();
			exit(1);
		default:
			image_usage();
			/* NOT REACHED */
		}
	}
	argc -= optind;
	argv += optind;
	if (ic.i_do_remove) {
		if (argc == 0) {
			image_usage();
		}
		image_remove(argc, argv, ctlsock);
		return (0);
	}
	if (ic.i_do_prune) {
		image_prune(&ic, ctlsock);
		return (0);
	}
	image_get(&ic, ctlsock);
	return (0);
}
