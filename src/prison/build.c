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

struct build_manifest *
build_manifest_load(struct build_config *bcp)
{
	struct build_manifest *bmp;
	char manifest_path[128];
	FILE *f;

	bmp = build_manifest_init();
	if (bmp == NULL) {
		err(1, "failed to get build manifest");
	}
	(void) snprintf(manifest_path, sizeof(manifest_path), "%s/%s",
	    bcp->b_path, bcp->b_prison_file);
	f = fopen(manifest_path, "r");
	if (f == NULL) {
		err(1, "fopen manifest failed");
	}
	yyfile = manifest_path;
	yyin = f;
	set_current_build_manifest(bmp);
	yyparse();
	fclose(f);
	return (bmp);
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

static void
build_process_stages(struct build_manifest *bmp)
{
	struct build_stage *bsp;

	TAILQ_FOREACH(bsp, &bmp->stage_head, stage_glue) {
		printf("-- FROM %s %p\n", bsp->bs_base_container, bsp);
	}
}

int
build_main(int argc, char *argv [], int cltlsock)
{
	struct build_manifest *bmp;
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
	bmp = build_manifest_load(&bc);
	build_process_stages(bmp);
	return (0);
}
