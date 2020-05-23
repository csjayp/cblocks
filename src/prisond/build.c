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
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <termios.h>
#include <libutil.h>
#include <signal.h>
#include <string.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"
#include "config.h"

#include <libprison.h>

TAILQ_HEAD( , build_context) bc_head;

struct build_context *
build_lookup_queued_context(struct prison_build_context *pbc)
{
	struct build_context *b;

	TAILQ_FOREACH(b, &bc_head, bc_glue) {
		if (strcmp(pbc->p_image_name, b->pbc.p_image_name) == 0 &&
		    strcmp(pbc->p_tag, b->pbc.p_tag) == 0) {
			return (b);
		}
	}
	return (NULL);
}

static int
build_emit_add_instruction(struct build_step *bsp, FILE *fp)
{
	struct build_step_add *sap;

	assert(bsp->step_op == STEP_ADD);
	sap = &bsp->step_data.step_add;
	switch (sap->sa_op) {
	case ADD_TYPE_FILE:
		fprintf(fp, "cp -pr \"${stage_tmp_dir}/%s\" %s\n",
		    sap->sa_source, sap->sa_dest);
		break;
	case ADD_TYPE_ARCHIVE:
		fprintf(fp, "tar -C %s -zxf \"${stage_tmp_dir}/%s\"\n",
		    sap->sa_dest, sap->sa_source);
		break;
	case ADD_TYPE_URL:
		fprintf(fp, "fetch -o %s %s\n", sap->sa_dest, sap->sa_source);
		break;
	default:
		warnx("invalid ADD operand %d", sap->sa_op);
		return (-1);
	}
	return (0);
}

static char *
build_get_stage_deps(struct build_context *bcp, int stage_index)
{
	struct build_step *step;
	char retbuf[256], *p;
	int k, j;

	bzero(retbuf, sizeof(retbuf));
	for (k = 0; k < bcp->pbc.p_nsteps; k++) {
		step = &bcp->steps[k];
		if (step->stage_index != stage_index) {
			continue;
		}
		if (step->step_op != STEP_COPY_FROM) {
			continue;
		}
		j = step->step_data.step_copy_from.sc_stage;
		/*
		 * NB: check the size, we need to revisit this
		 * and fix it so that it's not using a statically
		 * sized stack buffer.
		 */
		sprintf(retbuf, "%s %d", retbuf, j);
	}
	p = retbuf;
	/*
	 * Trim leading space.
	 */
	p++;
	return (strdup(p));
}

static int
build_emit_shell_script(struct build_context *bcp, int stage_index)
{
	char script[MAXPATHLEN];
	struct build_step *bsp;
	int steps, k, header, taken;
	FILE *fp;

	snprintf(script, sizeof(script), "%s.%d.sh",
	    bcp->build_root, stage_index);
	fp = fopen(script, "w+");
	if (fp == NULL) {
		err(1, "failed to create bootstrap script");
	}
	for (steps = 0, k = 0; k < bcp->pbc.p_nsteps; k++) {
		bsp = &bcp->steps[k];
		if (bsp->stage_index != stage_index) {
			continue;
		}
		steps++;
	}
	header = 0;
	for (taken = 0, k = 0; k < bcp->pbc.p_nsteps; k++) {
		bsp = &bcp->steps[k];
		if (bsp->stage_index != stage_index) {
			continue;
		}
		if (!header) {
			fprintf(fp, "#!/bin/sh\n\n");
			fprintf(fp, ". /prison_build_variables.sh\n");
			fprintf(fp, "set -e\n");
			if (bcp->pbc.p_verbose > 0) {
				fprintf(fp, "set -x\n");
			}
			header = 1;
		}
		fprintf(fp, "echo '-- Step %d/%d : %s'\n",
		    ++taken, steps, bsp->step_string);
		switch (bsp->step_op) {
		case STEP_ENV:
			fprintf(fp, "export %s=\"%s\"\n",
			    bsp->step_data.step_env.se_key,
			    bsp->step_data.step_env.se_value);
			break;
		case STEP_ROOT_PIVOT:
			fprintf(fp, "ln -s %s /cellblock-root-ptr\n",
			    bsp->step_data.step_root_pivot.sr_dir);
			break;
		case STEP_ADD:
			build_emit_add_instruction(bsp, fp);
			break;
		case STEP_COPY:
			fprintf(fp, "cp -pr \"${stage_tmp_dir}/%s\" %s\n",
			    bsp->step_data.step_copy.sc_source,
			    bsp->step_data.step_copy.sc_dest);
			break;
		case STEP_RUN:
			fprintf(fp, "%s\n", bsp->step_data.step_cmd);
			break;
		case STEP_COPY_FROM:
			fprintf(fp, "cp -pr \"${stages}/%d/%s\" %s\n",
			    bsp->step_data.step_copy_from.sc_stage,
			    bsp->step_data.step_copy_from.sc_source,
			    bsp->step_data.step_copy_from.sc_dest);
			break;
		case STEP_WORKDIR:
			fprintf(fp, "cd %s\n",
			    bsp->step_data.step_workdir.sw_dir);
			break;
		}
	}
	fclose(fp);
	return (0);
}

static int
build_init_stage(struct build_context *bcp, struct build_stage *stage)
{
	char script[128], index[16], context_archive[128], **argv;
	vec_t *vec;
	int status;
	pid_t pid;

	(void) snprintf(script, sizeof(script),
	    "%s/lib/stage_init.sh", gcfg.c_data_dir);
	(void) snprintf(index, sizeof(index), "%d", stage->bs_index);
	(void) snprintf(context_archive, sizeof(context_archive),
	    "%s/instances/%s.tar.gz", gcfg.c_data_dir, bcp->instance);
	pid = fork();
	if (pid == -1) {
		err(1, "fork failed");
	}
	if (pid == 0) {
		vec = vec_init(32);
		vec_append(vec, "/bin/sh");
		vec_append(vec, script);
		vec_append(vec, bcp->build_root);
		vec_append(vec, index);
		vec_append(vec, stage->bs_base_container);
		vec_append(vec, gcfg.c_data_dir);
		vec_append(vec, context_archive);
		vec_append(vec, build_get_stage_deps(bcp, stage->bs_index));
		vec_append(vec, bcp->instance);
		if (stage->bs_name[0] != '\0') {
			vec_append(vec, stage->bs_name);
		}
		if (vec_finalize(vec) != 0) {
			errx(1, "failed to construct command line");
		}
		argv = vec_return(vec);
		execve(*argv, argv, NULL);
		vec_free(vec);
		err(1, "execv failed");
	}
	while (1) {
		pid = waitpid(pid, &status, 0);
		if (pid == -1 && errno == EINTR) {
			continue;
		} else if (pid == -1) {
			err(1, "waitpid failed");
		}
		break;
	}
	return (status);
}

static int
build_process_wait(pid_t pid)
{
	int ret, status;

	while (1) {
		ret = waitpid(pid, &status, 0);
		if (ret == -1 && errno == EINTR) {
			continue;
		}
		if (ret == -1) {
			err(1, "waitpid failed(%d)", pid);
		}
		break;
	}
	return (status);
}

static int
build_commit_image(struct build_context *bcp)
{
	char commit_cmd[128], **argv, s_index[32], nstages[32];
	FILE *fp;
	struct build_stage *bsp;
	char *do_fim;
	int status, k, last;
	char path[1024];
	vec_t *vec;
	pid_t pid;

	last = -1;
	for (k = 0; k < bcp->pbc.p_nstages; k++) {
		bsp = &bcp->stages[k];
		if (bsp->bs_is_last == 0) {
			continue;
		}
		last = bsp->bs_index;
		break;
	}
	/*
	 * Write out entry point and enty point args (CMD) for the final stage
	 */
	if (bcp->pbc.p_entry_point[0] != '\0') {
		snprintf(path, sizeof(path), "%s/%d/ENTRYPOINT",
		    bcp->build_root, last);
		fp = fopen(path, "w+");
		if (fp == NULL) {
			err(1, "fopen(%s) failed", path);
		}
		fprintf(fp, "%s", bcp->pbc.p_entry_point);
		fclose(fp);
	}
	if (bcp->pbc.p_entry_point_args[0] != '\0') {
		snprintf(path, sizeof(path), "%s/%d/ARGS",
		    bcp->build_root, last);
		fp = fopen(path, "w+");
		if (fp == NULL) {
			err(1, "fopen(%s) failed", path);
		}
		fprintf(fp, "%s",
		    bcp->pbc.p_entry_point_args);
		fclose(fp);
	}
	pid = fork();
	if (pid == -1) {
		err(1, "%s: fork failed", __func__);
	}
	if (pid != 0) {
		status = build_process_wait(pid);
		if (status != 0) {
			warnx("failed to commit image");
		}
		return (status);
	}
	snprintf(commit_cmd, sizeof(commit_cmd), "%s/lib/commit_script.sh",
	    gcfg.c_data_dir);
	snprintf(nstages, sizeof(nstages), "%d", bcp->pbc.p_nstages);
	snprintf(s_index, sizeof(s_index), "%d", last);
	do_fim = "OFF";
	vec = vec_init(16);
	vec_append(vec, "/bin/sh");
	vec_append(vec, commit_cmd);
	vec_append(vec, bcp->build_root);
	vec_append(vec, s_index);
	vec_append(vec, gcfg.c_data_dir);
	vec_append(vec, bcp->pbc.p_image_name);
	vec_append(vec, nstages);
	vec_append(vec, bcp->instance);
	if (bcp->pbc.p_build_fim_spec) {
		do_fim = "ON";
	}
	vec_append(vec, do_fim);
	vec_finalize(vec);
	argv = vec_return(vec);
	execve(*argv, argv, NULL);
	err(1, "execve: image commit failed");
	/* NOT REACHED */
	return (1);
}

static int
build_run_build_stage(struct build_context *bcp)
{
	char stage_root[MAXPATHLEN], **argv;
	struct build_stage *bstg;
	int k, status, ret;
	vec_t *vec;
	pid_t pid;

	for (k = 0; k < bcp->pbc.p_nstages; k++) {
		bstg = &bcp->stages[k];
		printf("-- Executing stage (%d/%d)\n", k + 1, bcp->pbc.p_nstages);
		snprintf(stage_root, sizeof(stage_root),
		    "%s/%d/root", bcp->build_root, bstg->bs_index);
		pid = fork();
		if (pid == -1) {
			err(1, "pid failed");
		}
		if (pid == 0) {
			vec = vec_init(32);
			vec_append(vec, "/bin/sh");
			vec_append(vec, "prison-bootstrap.sh");
			vec_finalize(vec);
			argv = vec_return(vec);
			if (chdir(stage_root) == -1) {
				err(1, "chdir failed");
			}
			if (chroot(".") == -1) {
				err(1, "chroot failed");
			}
			execve(*argv, argv, NULL);
			err(1, "execve failed");
		}
		while (1) {
			ret = waitpid(pid, &status, 0);
			if (ret == pid) {
				break;
			}
			if (ret == -1 && errno == EINTR) {
				continue;
			}
			if (ret == -1) {
				err(1, "waitpid failed");
			}
		}
	}
	bstg = &bcp->stages[k - 1];
	bstg->bs_is_last = 1;
	if (status != 0) {
		printf("-- Stage build failed: %d code\n", status);
	}
	return (status);
}

static int
build_run_init_stages(struct build_context *bcp)
{
	char stage_root[MAXPATHLEN];
	struct build_stage *bstg;
	int k, r;

	printf("DEBUG: %s\n", bcp->instance);
	snprintf(bcp->build_root, sizeof(bcp->build_root),
	    "%s/instances/%s", gcfg.c_data_dir, bcp->instance);
	for (k = 0; k < bcp->pbc.p_nstages; k++) {
		bstg = &bcp->stages[k];
		printf("-- Executing stage %d\n", bstg->bs_index);
		snprintf(stage_root, sizeof(stage_root),
		    "%s/%d", bcp->build_root, bstg->bs_index);
		if (mkdir(stage_root, 0755) == -1) {
			err(1, "mkdir(%s) failed", stage_root);
		}
		snprintf(stage_root, sizeof(stage_root),
		    "%s/%d/root", bcp->build_root, bstg->bs_index);
		if (mkdir(stage_root, 0755) == -1) {
			err(1, "mkdir(%s) failed", stage_root);
		}
		build_emit_shell_script(bcp, bstg->bs_index);
		r = build_init_stage(bcp, bstg);
		if (r != 0) {
			printf("-- Stage failed with %d code. Exiting", r);
			break;
		}
	}
	return (r);
}

static int
dispatch_build_set_outfile(struct build_context *bcp,
    char *ebuf, size_t len)
{
	char path[512], build_root[512];
	int fd;

	(void) snprintf(path, sizeof(path),
	    "%s/instances/%s.tar.gz", gcfg.c_data_dir, bcp->instance);
	fd = open(path, O_RDWR | O_EXCL | O_CREAT, 0600);
	if (fd == -1) {
		snprintf(ebuf, len, "could not write to build spool: %s",
		    strerror(errno));
		return (-1);
	}
	(void) snprintf(build_root, sizeof(build_root),
	    "%s/instances/%s", gcfg.c_data_dir, bcp->instance);
	if (mkdir(build_root, 0755) == -1) {
		snprintf(ebuf, len, "failed to initialize build env: %s",
		    strerror(errno));
		(void) unlink(path);
		close(fd);
		return (-1);
	}
	return (fd);
}

static void
copy_build_context(struct build_context *from, struct build_context *to)
{
	struct build_stage *bstp, *bstp2;
	struct build_step *bsp, *bsp2;
	int k;

	*to = *from;
	to->instance = strdup(from->instance);
	to->steps = calloc(from->pbc.p_nsteps, sizeof(*bsp));
	for (k = 0; k < from->pbc.p_nsteps; k++) {
		bsp = &from->steps[k];
		bsp2 = &to->steps[k];
		*bsp2 = *bsp;
	}
	to->stages = calloc(from->pbc.p_nstages, sizeof(*bstp));
	for (k = 0; k < from->pbc.p_nstages; k++) {
		bstp = &from->stages[k];
		bstp2 = &to->stages[k];
		*bstp2 = *bstp;
	}
}

int
do_build_launch(void *arg, struct prison_instance *pi)
{
	struct build_context *bcp;

	bcp = (struct build_context *)arg;
	if (build_run_build_stage(bcp) != 0) {
		return (-1);
	}
	printf("-- Stages complete. Writing container image...\n");
	if (build_commit_image(bcp) != 0) {
		return (-1);
	}
	return (0);
}

int
dispatch_build_recieve(int sock)
{
	struct build_context bctx, *bctxp;
	struct prison_response resp;
	ssize_t cc;
	int fd;

	bzero(&bctx, sizeof(bctx));
	printf("executing build recieve\n");
	cc = sock_ipc_must_read(sock, &bctx.pbc, sizeof(bctx.pbc));
	if (cc == 0) {
		printf("didn't get proper build context headers\n");
		return (0);
	}
	printf("got context\n");
	if (bctx.pbc.p_nstages > MAX_BUILD_STAGES ||
	    bctx.pbc.p_nsteps > MAX_BUILD_STEPS) {
		resp.p_ecode = -1;
		sprintf(resp.p_errbuf, "too many build stages/steps\n");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	bctx.stages = calloc(bctx.pbc.p_nstages, sizeof(*bctx.stages));
	if (bctx.stages == NULL) {
		resp.p_ecode = -1;
		sprintf(resp.p_errbuf, "out of memory");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	bctx.steps = calloc(bctx.pbc.p_nsteps, sizeof(*bctx.steps));
	if (bctx.steps == NULL) {
		resp.p_ecode = -1;
		sprintf(resp.p_errbuf, "out of memory");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	sock_ipc_must_read(sock, bctx.stages,
	    bctx.pbc.p_nstages * sizeof(*bctx.stages));
	printf("read stages\n");
	sock_ipc_must_read(sock, bctx.steps,
	    bctx.pbc.p_nsteps * sizeof(*bctx.steps));
	printf("read steps\n");
	bctx.instance = gen_sha256_instance_id(bctx.pbc.p_image_name);
	fd = dispatch_build_set_outfile(&bctx, resp.p_errbuf,
	    sizeof(resp.p_errbuf));
	if (fd == -1) {
		free(bctx.steps);
		free(bctx.stages);
		resp.p_ecode = -1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
        }
	printf("got fd %d\n", fd);
	printf("reading sock from to\n");
	if (sock_ipc_from_to(sock, fd, bctx.pbc.p_context_size) == -1) {
		err(1, "sock_ipc_from_to failed");
	}
	printf("build_run_init_stages ...\n");
	if (build_run_init_stages(&bctx) != 0) {
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		close(fd);
		return (1);
	}
	bctxp = calloc(1, sizeof(*bctxp));
	copy_build_context(&bctx, bctxp);
	TAILQ_INSERT_HEAD(&bc_head, bctxp, bc_glue);
	close(fd);
	free(bctx.instance);
	bzero(&resp, sizeof(resp));
	resp.p_ecode = 0;
	snprintf(resp.p_errbuf, sizeof(resp.p_errbuf),
	    "%s", bctxp->instance);
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	return (1);
}
