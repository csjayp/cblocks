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

#include "main.h"
#include "parser.h"

#include <libcblock.h>

struct build_config {
	char			*b_name;
	char			*b_cblock_file;
	char			*b_path;
	char			*b_context_path;
	char			*b_tag;
	struct build_manifest	*b_bmp;
	int			 b_verbose;
	int			 b_fim_spec;
};

static struct option build_options[] = {
	{ "name",		required_argument, 0, 'n' },
	{ "cblock-file-path",	required_argument, 0, 'f' },
	{ "tag",		required_argument, 0, 't' },
	{ "no-exec",		no_argument, 0, 'N' },
	{ "help",		no_argument, 0, 'h' },
	{ "verbose",		no_argument, 0, 'v' },
	{ "file-integrity",	no_argument, 0, 'F' },
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
	    bcp->b_path, bcp->b_cblock_file);
	f = fopen(manifest_path, "r");
	if (f == NULL) {
		err(1, "fopen manifest failed");
	}
	yyfile = manifest_path;
	yyin = f;
	set_current_build_manifest(bmp);
	bcp->b_bmp = bmp;
	yyparse();
	fclose(f);
	return (bmp);
}

static void
build_usage(void)
{
	(void) fprintf(stderr,
	    "Usage: cblock build [OPTIONS] PATH\n\n"
	    "Options\n"
	    " -h, --help                    Print help\n"
	    " -n, --name=NAME               Name of container image to build\n"
	    " -f, --cblock-file-path=PATH   Path to Cblockfile (relative to build path)\n"
	    " -t, --tag=NAME                Tag to use for the image build\n"
	    " -N, --no-exec                 Do everything but submit the build context\n"
	    " -v, --verbose                 Increase verbosity of build\n"
	    " -F, --file-integrity          Create file integrity spec\n");
	exit(1);
}

static void
build_init_stage_count(struct build_config *bcp,
    struct cblock_build_context *pbc)
{
	struct build_stage *bsp;
	struct build_step *bs;

	pbc->p_nstages = 0;
	pbc->p_nsteps = 0;
	TAILQ_FOREACH(bsp, &bcp->b_bmp->stage_head, stage_glue) {
		pbc->p_nstages++;
		TAILQ_FOREACH(bs, &bsp->step_head, step_glue) {
			pbc->p_nsteps++;
		}
	}
}

static void
build_send_stages(int sock, struct build_config *bcp)
{
	struct build_stage *stage;
	struct build_step *step;

	TAILQ_FOREACH_REVERSE(stage, &bcp->b_bmp->stage_head,
	    tailhead_stage, stage_glue) {
		sock_ipc_must_write(sock, stage, sizeof(*stage));
	}
	TAILQ_FOREACH_REVERSE(stage, &bcp->b_bmp->stage_head,
	    tailhead_stage, stage_glue) {
		TAILQ_FOREACH_REVERSE(step, &stage->step_head,
		    tailhead_step, step_glue) {
			sock_ipc_must_write(sock, step, sizeof(*step));
		}
	}
}

static int
build_send_context(int sock, struct build_config *bcp)
{
	struct cblock_build_context pbc;
	struct stat sb;
	char *term;
	u_int cmd;
	int fd;

	if (stat(bcp->b_context_path, &sb) == -1) {
		err(1, "stat failed");
	}
	fd = open(bcp->b_context_path, O_RDONLY);
	if (fd == -1) {
		err(1, "error opening build context");
	}
	term = getenv("TERM");
	if (term == NULL) {
		errx(1, "Can not determine TERM type\n");
	}
	bzero(&pbc, sizeof(pbc));
	cmd = PRISON_IPC_SEND_BUILD_CTX;
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	pbc.p_build_fim_spec = bcp->b_fim_spec;
	pbc.p_context_size = sb.st_size;
	pbc.p_verbose = bcp->b_verbose;
	strlcpy(pbc.p_term, term, sizeof(pbc.p_term));
	strlcpy(pbc.p_image_name, bcp->b_name, sizeof(pbc.p_image_name));
	strlcpy(pbc.p_cblock_file, bcp->b_cblock_file,
	    sizeof(pbc.p_cblock_file));
	if (bcp->b_bmp->entry_point) {
		strlcpy(pbc.p_entry_point, bcp->b_bmp->entry_point,
		    sizeof(pbc.p_entry_point));
	}
	if (bcp->b_bmp->entry_point_args) {
		strlcpy(pbc.p_entry_point_args, bcp->b_bmp->entry_point_args,
		    sizeof(pbc.p_entry_point_args));
	}
	strlcpy(pbc.p_tag, bcp->b_tag, sizeof(pbc.p_tag));
	build_init_stage_count(bcp, &pbc);
	sock_ipc_must_write(sock, &pbc, sizeof(pbc));
	build_send_stages(sock, bcp);
	print_bold_prefix(stdout);
	fprintf(stdout,
	    "Transmitting build context to cblock daemon (%zu) bytes...\n",
	    sb.st_size);
	fflush(stdout);
	if (sock_ipc_from_to(fd, sock, sb.st_size) == -1) {
		err(1, "sock_ipc_from_to: failed");
	}
	close(fd);
	if (unlink(bcp->b_context_path) == -1) {
		err(1, "failed to cleanup build context");
	}
	sock_ipc_from_sock_to_tty(sock);
	return (0);
}

static int
build_generate_context(struct build_config *bcp)
{
	char *argv[10], *build_context_path, *template, dst[256];
	int status;
	pid_t pid;

	template = strdup("/tmp/cblock-bcontext.XXXXXXXXX");
	build_context_path = mktemp(template);
	if (build_context_path == NULL) {
		err(1, "failed to generate random file");
	}
	snprintf(dst, sizeof(dst), "%s.tar.gz", build_context_path);
	build_context_path = mktemp(template);
	print_bold_prefix(stdout),
	fprintf(stdout, "Preparing local build context...\n");
	fflush(stdout);
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
	waitpid_ignore_intr(pid, &status);
	if (rename(build_context_path, dst) == -1) {
		err(1, "could not rename build context");
	}
	free(template);
	bcp->b_context_path = strdup(dst);
	if (bcp->b_context_path == NULL) {
		err(1, "strdup failed");
	}
	return (status);
}

static void
build_set_default_tag(struct build_config *bcp)
{
	char buf[32], *p;

	if (bcp->b_tag != NULL) {
		return;
	}
	snprintf(buf, sizeof(buf), "latest");
	p = strdup(buf);
	if (p == NULL) {
		err(1, "strdup failed");
	}
	bcp->b_tag = p;
}

int
build_main(int argc, char *argv [], int cltlsock)
{
	struct build_manifest *bmp;
	struct build_config bc;
	time_t before, after;
	int option_index;
	int c, noexec;
	char *tag, *ptr;

	noexec = 0;
	bzero(&bc, sizeof(bc));
	bc.b_cblock_file = "Cblockfile";
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "FNhf:n:t:v", build_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'F':
			bc.b_fim_spec = 1;
			break;
		case 'v':
			bc.b_verbose++;
			break;
		case 'N':
			noexec = 1;
			break;
		case 'h':
			build_usage();
			exit(1);
		case 'n':
			bc.b_name = optarg;
			break;
		case 'f':
			bc.b_cblock_file = optarg;
			break;
		case 't':
			bc.b_tag = optarg;
			break;
		default:
			build_usage();
			/* NOT REACHED */
		}
	}
	argc -= optind;
	argv += optind;
	if (bc.b_name == NULL) {
		errx(1, "must specify image name -n");
	}
	tag = strchr(bc.b_name, ':');
	if (tag != NULL) {
		/*
		 * Set the ':' character to null which will terminate the
		 * string right after the base image name. Then we can
		 * extract the tag and store it seperately.
		 */
		*tag = '\0';
		tag++;
		ptr = strdup(tag);
		bc.b_tag = ptr;
	}
	bc.b_path = argv[0];
	if (!bc.b_path) {
		fprintf(stderr, "ERROR: no build path specified\n");
		build_usage();
	}
	before = time(NULL);
	build_set_default_tag(&bc);
	bmp = build_manifest_load(&bc);
	if (noexec) {
		return (0);
	}
	build_generate_context(&bc);
	build_send_context(cltlsock, &bc);
	after = time(NULL);
	printf("build occured in %ld seconds\n", after - before);
	return (0);
}
