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
#include <sys/stat.h>
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/ttycom.h>

#include <stdio.h>
#include <errno.h>
#include <assert.h>
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
	char		*b_context_path;
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

static int
build_send_context(int sock, struct build_config *bcp)
{
	struct stat sb;
	int fd;
	u_int cmd;
	struct prison_build_context pbc;
	struct prison_response resp;

	if (stat(bcp->b_context_path, &sb) == -1) {
		err(1, "stat failed");
	}
	fd = open(bcp->b_context_path, O_RDONLY);
	if (fd == -1) {
		err(1, "error opening build context");
	}
	printf("Sending build context (%zu) bytes total\n", sb.st_size);
	cmd = PRISON_IPC_SEND_BUILD_CTX;
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	pbc.p_context_size = sb.st_size;
	strlcpy(pbc.p_image_name, bcp->b_name, sizeof(pbc.p_image_name));
	strlcpy(pbc.p_prison_file, bcp->b_prison_file, sizeof(pbc.p_prison_file));
	sock_ipc_must_write(sock, &pbc, sizeof(pbc));
	if (sock_ipc_from_to(fd, sock, sb.st_size) == -1) {
		err(1, "sock_ipc_from_to: failed");
	}
	sock_ipc_must_read(sock, &resp, sizeof(resp));
	printf("read status code %d from daemon\n", resp.p_ecode);
	return (0);
}

static int
build_generate_context(struct build_config *bcp)
{
	char *argv[10], *build_context_path, *template;
	char dst[256];
	int ret, status, pid;

	printf("Constructing build context...");
	fflush(stdout);
	template = strdup("/tmp/prison-bcontext.XXXXXXXXX");
	build_context_path = mktemp(template);
	if (build_context_path == NULL) {
		err(1, "failed to generate random file");
	}
	snprintf(dst, sizeof(dst), "%s.tar.gz", build_context_path);
	build_context_path = mktemp(template);
	pid = fork();
	if (pid == -1) {
		err(1, "fork faild");
	}
	if (pid == 0) {
		argv[0] = "/usr/bin/tar";
		argv[1] = "-C";
		argv[2] = bcp->b_path;
		argv[3] = "-cpf";
		argv[4] = build_context_path;
		argv[5] = ".";
		argv[6] = NULL;
		execve(*argv, argv, NULL);
		err(1, "failed to exec tar for build context");
	}
	while (1) {
		ret = waitpid(pid, &status, 0);
		if (ret == -1 && errno == EINTR) {
			continue;
		} else if (ret == -1) {
			err(1, "waitpid faild");
		}
		break;
		assert(ret == pid);
	}
	if (rename(build_context_path, dst) == -1) {
		err(1, "could not rename build context");
	}
	printf("DONE\n");
	free(template);
	bcp->b_context_path = strdup(dst);
	if (bcp->b_context_path == NULL) {
		err(1, "strdup failed");
	}
	return (status);
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
		c = getopt_long(argc, argv, "f:n:t:", build_options,
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
	build_generate_context(&bc);
	build_send_context(cltlsock, &bc);
	return (0);
}
