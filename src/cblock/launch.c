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
#include <sys/ioctl.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <err.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <cblock/libcblock.h>

#include "main.h"

struct launch_config {
	char		*l_name;
	char		*l_terminal;
	vec_t		*l_vec;
	char		*l_volumes;
	char		*l_network;
	int		 l_attach;
	int		 l_verbose;
	char		*l_tag;
	char		*l_ports;
	int		 l_host_networking;
};

static struct option launch_options[] = {
	{ "name",		required_argument, 0, 'n' },
	{ "terminal",		required_argument, 0, 't' },
	{ "network",		required_argument, 0, 'N' },
	{ "volume",		required_argument, 0, 'V' },
	{ "fdescfs",		no_argument, 0, 'F' },
	{ "procfs",		no_argument, 0, 'p' },
	{ "tmpfs",		no_argument, 0, 'T' },
	{ "help",		no_argument, 0, 'h' },
	{ "no-attach",		no_argument, 0, 'A' },
	{ "verbose",		no_argument, 0, 'v' },
	{ "port",		required_argument, 0, 'P' },
	{ "host-networking",	no_argument, 0, 'H' },
	{ 0, 0, 0, 0 }
};

static void
launch_usage(void)
{
	(void) fprintf(stderr,
	    " -h, --help                 Print help\n"
	    " -n, --name=NAME            Name of container image to launch\n"
	    " -t, --terminal=TERM        Terminal type to use (TERM)\n"
	    " -N, --network=NETIF        Attach container to bridge or NAT interface\n"
	    " -V, --volume=VOLUMESPEC    Mount volume into the container\n"
	    " -F, --fdescfs              Mount file-descriptor file system\n"
	    " -T, --tmpfs                Mount in-memory ephemeral tmpfs\n"
	    " -p, --procfs               Mount process file system\n"
	    " -P, --port=PORTSPEC        Expose container port(s)\n"
	    " -A, --no-attach            Do not attach to container console\n"
	    " -v, --verbose              Launch container with verbosity enabled\n"
	    " -H, --host-networking      Use host networking instead of NAT/bridge\n"
	);
	exit(1);
}

static int
launch_container(int sock, struct launch_config *lcp)
{
	struct cblock_launch pl;
	struct cblock_response resp;
	char *term, *args;
	uint32_t cmd;
	vec_t *vec;

	if (lcp->l_terminal != NULL) {
		term = lcp->l_terminal;
	} else {
		term = getenv("TERM");
	}
	if (term == NULL) {
		term = CBLOCK_DEFAULT_TERM;
	}
	bzero(&pl, sizeof(pl));
	cmd = PRISON_IPC_LAUNCH_PRISON;
	if (lcp->l_vec != NULL) {
		args = vec_join(lcp->l_vec, ' ');
		if (args == NULL) {
			err(1, "failed to alloc memory for vec");
		}
		snprintf(pl.p_entry_point_args,
		    sizeof(pl.p_entry_point_args), "%s", args);
		free(args);
		vec_free(lcp->l_vec);
	}
	pl.p_verbose = lcp->l_verbose;
	snprintf(pl.p_tag, sizeof(pl.p_tag), "%s", lcp->l_tag);
	snprintf(pl.p_name, sizeof(pl.p_name), "%s", lcp->l_name);
	snprintf(pl.p_term, sizeof(pl.p_term), "%s", term);
	snprintf(pl.p_volumes, sizeof(pl.p_volumes), "%s", lcp->l_volumes);
	snprintf(pl.p_ports, sizeof(pl.p_ports), "%s", lcp->l_ports);
	snprintf(pl.p_network, sizeof(pl.p_network), "%s", lcp->l_network);
	if (sock_ipc_write_u32(sock, cmd) == -1 ||
	    proto_send_launch(sock, &pl) == -1) {
		errx(1, "failed to send launch request");
	}
	if (proto_recv_response(sock, &resp) == -1) {
		errx(1, "failed to read launch response");
	}
	if (resp.p_ecode != 0) {
		warnx("failed to spawn container");
		return (1);
	}
	printf("cellblock: container launched: instance: %s\n", resp.p_errbuf);
	if (lcp->l_attach) {
		vec = vec_init(16);
		vec_append(vec, "console");
		vec_append(vec, "--name");
		vec_append(vec, resp.p_errbuf);
		vec_finalize(vec);
		console_main(vec->vec_used, vec_return(vec), sock);
		vec_free(vec);
	}
	return (0);
}

int
launch_main(int argc, char *argv [], int ctlsock)
{
	struct launch_config lc;
	int option_index, c;
	vec_t *volumes, *ports;
	char *tag, *ptr;

	bzero(&lc, sizeof(lc));
	/*
	 * Each option consumes at least one argument, so argc bounds the
	 * number of volumes and ports.
	 */
	volumes = vec_init(argc + 1);
	ports = vec_init(argc + 1);
	if (volumes == NULL || ports == NULL) {
		err(1, "vec_init failed");
	}
	vec_append(volumes, "devfs");
	lc.l_tag = "latest";
	lc.l_attach = 1;
	lc.l_verbose = 0;
	/*
	 * We will use host networking if nothing else is specified. This hopefully
	 * simplifies the container launching use cases a bit.
	 */
	lc.l_network = "__host__";
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "AHP:vN:Fpn:t:V:T", launch_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'H':
			lc.l_host_networking = 1;
			break;
		case 'P':
			vec_append(ports, optarg);
			break;
		case 'v':
			lc.l_verbose = 1;
			break;
		case 'A':
			lc.l_attach = 0;
			break;
		case 'T':
			vec_append(volumes, "tmpfs");
			break;
		case 'N':
			lc.l_network = optarg;
			break;
		case 'F':
			vec_append(volumes, "fdescfs");
			break;
		case 'p':
			vec_append(volumes, "procfs");
			break;
		case 'V':
			vec_append(volumes, optarg);
			break;
		case 'h':
			launch_usage();
			exit(1);
		case 't':
			lc.l_terminal = optarg;
			break;
		case 'n':
			lc.l_name = optarg;
			break;
		default:
			launch_usage();
			/* NOT REACHED */
		}
	}
	if (lc.l_name == NULL) {
		fprintf(stderr, "must supply container name\n");
		launch_usage();
	}
	tag = strchr(lc.l_name, ':');
	if (tag != NULL) {
		/*
		 * Set the ':' character to null which will terminate the
		 * string right after the base image name. Then we can
		 * extract the tag and store it seperately.
		 */
		*tag = '\0';
		tag++;
		ptr = strdup(tag);
		lc.l_tag = ptr;
        }
	lc.l_volumes = vec_join(volumes, ',');
	lc.l_ports = "";
	if (ports->vec_used > 0) {
		lc.l_ports = vec_join(ports, ',');
	}
	if (lc.l_volumes == NULL || lc.l_ports == NULL) {
		err(1, "vec_join failed");
	}
	if (lc.l_host_networking) {
		if (ports->vec_used > 0) {
			warnx("Port mappings are not supported with host networking");
			warnx("Create a NAT based network if you want this.");
			exit(1);
		}
		if (lc.l_network && strcmp(lc.l_network, "__host__") != 0) {
			warnx("--network and --host-networking are mutually exclusive");
			exit(1);
		}
		lc.l_network = "__host__";
	}
	if (lc.l_network == NULL) {
		warnx("Must specify network to attach container to");
		warnx("Use one of: --network, --host-networking");
		exit(1);
	}
		
	argc -= optind;
	argv += optind;
	/*
	 * Check to see if the user has spcified command line arguments to
	 * along to the entry point for this container.
	 */
	lc.l_vec = NULL;
	if (argc != 0) {
		lc.l_vec = vec_init(argc + 1);
		for (c = 0; c < argc; c++) {
			vec_append(lc.l_vec, argv[c]);
		}
		vec_finalize(lc.l_vec);
	}
	return (launch_container(ctlsock, &lc));
}
