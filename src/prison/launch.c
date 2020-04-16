#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <err.h>
#include <termios.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <libprison.h>

#include "main.h"

struct launch_config {
	char		*l_name;
	char		*l_terminal;
};

static struct option launch_options[] = {
	{ "name",		required_argument, 0, 'n' },
	{ "terminal",		required_argument, 0, 't' },
	{ "help",		no_argument, 0, 'h' },
	{ 0, 0, 0, 0 }
};

static void
launch_usage(void)
{
	(void) fprintf(stderr,
	    "Options\n"
	    " -h, --help                  Print help\n"
	    " -n, --name=NAME             Name of container image to launch\n"
	    " -t, --terminal=TERM         Terminal type to use (TERM)\n");
	exit(1);
}

static void
launch_container(int sock, struct launch_config *lcp)
{
	struct prison_launch pl;
	struct prison_response resp;
	uint32_t cmd;
	char *term;

	if (lcp->l_terminal != NULL) {
		printf("setting term\n");
		term = lcp->l_terminal;
	} else {
		term = getenv("TERM");
	}
	cmd = PRISON_IPC_LAUNCH_PRISON;
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	strlcpy(pl.p_name, lcp->l_name, sizeof(pl.p_name));
	strlcpy(pl.p_term, term, sizeof(pl.p_term));
	sock_ipc_must_write(sock, &pl, sizeof(pl));
	sock_ipc_must_read(sock, &resp, sizeof(resp));
	printf("got error code %d\n", resp.p_ecode);
}

int
launch_main(int argc, char *argv [], int ctlsock)
{
	struct launch_config lc;
	int option_index;
	int c;

	bzero(&lc, sizeof(lc));
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "n:t:", launch_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
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
	argc -= optind;
	argv += optind;
	launch_container(ctlsock, &lc);
	return (0);
}
