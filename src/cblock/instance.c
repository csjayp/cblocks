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
#include <signal.h>
#include <getopt.h>
#include <stdlib.h>
#include <err.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#include <cblock/libcblock.h>

#include "main.h"

struct instance_config {
	int		 i_long;
	int		 i_quiet;
	int		 i_do_prune;
	char		*i_instance;
	int		 i_sigop;
};

static struct option instance_options[] = {
	{ "long",		no_argument, 0, 'l' },
	{ "help",		no_argument, 0, 'h' },
	{ "kill",		required_argument, 0, 'k' },
	{ "quiet",		no_argument, 0, 'q' },
	{ "prune",		no_argument, 0, 'p' },
	{ "stop",		required_argument, 0, 's' },
	{ 0, 0, 0, 0 }
};

static void
instance_usage(void)
{
	(void) fprintf(stderr,
	    "Usage: cblock instances [OPTIONS]\n\n"
	    "Options\n"
	    " -h, --help                  Print help\n"
	    " -k, --kill                  Forcefully shutdown instance\n"
	    " -p, --prune                 Remove stopped/dead instances\n"
	    " -l, --long                  Print full instance names\n"
	    " -q, --quiet                 Do not print column headers\n"
	    " -s, --stop                  Gracefully shutdown instance\n");
	exit(1);
}

static void
instance_get(struct instance_config *icp, int ctlsock)
{
	struct instance_ent *ent, *cur;
	uint32_t cmd, k;
	size_t count;
	time_t now;

	count = 0;
	cmd = PRISON_IPC_GET_INSTANCES;
	sock_ipc_must_write(ctlsock, &cmd, sizeof(cmd));
	sock_ipc_must_read(ctlsock, &count, sizeof(count));
	if (count == 0) {
		return;
	}
	ent = malloc(count * sizeof(struct instance_ent));
	if (ent == NULL) {
		err(1, "malloc for instance list failed");
	}
	sock_ipc_must_read(ctlsock, ent, count * sizeof(struct instance_ent));
	if (!icp->i_quiet) {
		printf("%-10.10s  %-15.15s %-12.12s %-7.7s %-11.11s %10.10s\n",
		    "INSTANCE", "IMAGE", "TTY", "PID", "TYPE", "UP");
	}
	now = time(NULL);
	for (k = 0; k < count; k++) {
		cur = &ent[k];
		printf("%-10.10s  %-15.15s %-12.12s %-7d %-11.11s %9lds\n",
		    cur->p_instance_name,
		    cur->p_image_name,
		    cur->p_tty_line,
		    cur->p_pid,
		    cur->p_type,
		    now - cur->p_start_time);
	}
}

static void
instance_prune(struct instance_config *icp __attribute__((unused)), int ctlsock)
{
	struct cblock_generic_command arg;
	uint32_t cmd;

	cmd = PRISON_IPC_GENERIC_COMMAND;
	bzero(&arg, sizeof(arg));
	sock_ipc_must_write(ctlsock, &cmd, sizeof(cmd));
	sprintf(arg.p_cmdname, "instance_prune");
	sock_ipc_must_write(ctlsock, &arg, sizeof(arg));
	sock_ipc_from_sock_to_tty(ctlsock);
}

static void
instance_signal(struct instance_config *icp, int ctlsock)
{
	struct cblock_signal_instance csi;
	struct cblock_response resp;
	uint32_t cmd;

	cmd = PRISON_IPC_SIGNAL_INSTANCE;
	bzero(&csi, sizeof(csi));
	sprintf(csi.p_instance, "%s", icp->i_instance);
	/*
	 * We are using SIG constants but we probably need to abstract these
	 * for cross architecture/platform communications
	 */
	switch (icp->i_sigop) {
	case INSTANCE_SIGOP_KILL:
		csi.p_sig = SIGKILL;
		break;
	case INSTANCE_SIGOP_STOP:
		csi.p_sig = SIGTERM;
		break;
	default:
		(void) fprintf(stderr, "error: bad sigop: %d\n", icp->i_sigop);
		exit(1);
	}
	sock_ipc_must_write(ctlsock, &cmd, sizeof(cmd));
	sock_ipc_must_write(ctlsock, &csi, sizeof(csi));
	sock_ipc_must_read(ctlsock, &resp, sizeof(resp));
	if (resp.p_ecode != 0) {
		printf("ERROR: got error status back: %d msg: %s\n",
		    resp.p_ecode, resp.p_errbuf);
	}
}

int
instance_main(int argc, char *argv [], int ctlsock)
{
	struct instance_config ic;
	int option_index, c;

	bzero(&ic, sizeof(ic));
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "qhl", instance_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'k':
			ic.i_instance = optarg;
			ic.i_sigop = INSTANCE_SIGOP_KILL;
			break;
		case 'p':
			ic.i_do_prune = 1;
			break;
		case 'q':
			ic.i_quiet = 1;
			break;
		case 's':
			ic.i_instance = optarg;
			ic.i_sigop = INSTANCE_SIGOP_STOP;
			break;
		case 'h':
			instance_usage();
			exit(1);
		case 'l':
			ic.i_long = 1;
			break;
		default:
			instance_usage();
			/* NOT REACHED */
		}
	}
	argc -= optind;
	argv += optind;
	if (ic.i_sigop) {
		instance_signal(&ic, ctlsock);
		exit(0);
	}
	if (ic.i_do_prune) {
		instance_prune(&ic, ctlsock);
		exit(0);
	}
	instance_get(&ic, ctlsock);
	return (0);
}
