#include <sys/types.h>
#include <sys/queue.h>
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
#include "parser.h"
#include "build.h"

struct build_config {
	char		*b_name;
	char		*b_prison_file;
	char		*b_path;
};

static struct option build_options[] = {
	{ "name",		required_argument, 0, 'n' },
	{ "prison-file-path",	required_argument, 0, 'f' },
	{ "help",		no_argument, 0, 'h' },
	{ 0, 0, 0, 0 }
};

void
build_manifest_load(struct build_config *bcp)
{
	char manifest_path[128];
	FILE *f;

	(void) snprintf(manifest_path, sizeof(manifest_path), "%s/%s",
	    bcp->b_path, bcp->b_prison_file);
	f = fopen(manifest_path, "r");
	if (f == NULL) {
		err(1, "fopen manifest failed");
	}
	TAILQ_INIT(&manifest.stage_head);
	yyfile = manifest_path;
	yyin = f;
	yyparse();
	fclose(f);
}

static void
build_usage(void)
{
	(void) fprintf(stderr,
	    "Usage: prison build [OPTIONS] PATH\n\n"
	    "Options\n"
	    " -h, --help                    Print help\n"
	    " -n, --name=NAME               Name of container image to build\n"
	    " -f, --prison-file-path=PATH   Path to Prisonfile (relative to build path)\n");
	exit(1);
}

int
build_main(int argc, char *argv [], int cltlsock)
{
	struct build_config bc;
	int option_index;
	int c;

	bzero(&bc, sizeof(bc));
	bc.b_prison_file = "Prisonfile";
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "n:t:", build_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'h':
			build_usage();
			exit(1);
		case 'n':
			bc.b_name = optarg;
			break;
		case 'f':
			bc.b_prison_file = optarg;
			break;
		default:
			build_usage();
			/* NOT REACHED */
		}
	}
	argc -= optind;
	argv += optind;
	bc.b_path = argv[0];
	if (!bc.b_path) {
		fprintf(stderr, "ERROR: no build path specified\n");
		build_usage();
	}
	(void) fprintf(stdout, "building Prison at %s\n", bc.b_path);
	build_manifest_load(&bc);
	return (0);
}
