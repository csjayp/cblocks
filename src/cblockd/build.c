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
#include <stdarg.h>
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
#include "cblock.h"
#include "sock_ipc.h"
#include "config.h"
#include "probes.h"

#include <cblock/libcblock.h>

TAILQ_HEAD( , build_context) bc_head;

struct build_copy_from {
        int                             stage;
        vec_t                           *pathv;
        TAILQ_ENTRY(build_copy_from)    glue;
};
typedef TAILQ_HEAD( , build_copy_from) build_copy_from_t;

pid_t
waitpid_ignore_intr(pid_t pid, int *status)
{
	pid_t rpid;

	while (1) {
		rpid = waitpid(pid, status, 0);
		if (rpid == -1 && errno == EINTR) {
			continue;
		} else if (pid == -1) {
			err(1, "waitpid failed");
		}
		break;
	}
	return (rpid);
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
		fprintf(fp, "tar -C %s -zpxf \"${stage_tmp_dir}/%s\"\n",
		    sap->sa_dest, sap->sa_source);
		break;
	case ADD_TYPE_URL:
		fprintf(fp,
		    "fetch -q -o %s %s\n", sap->sa_dest, sap->sa_source);
		break;
	case ADD_TYPE_ARCHIVE_URL:
		fprintf(fp,
		    "_archive=$(mktemp)\n"
		    "fetch -q -o $_archive %s\n", sap->sa_source);
		fprintf(fp,
		    "tar -C %s -zpxf \"$_archive\"\n"
		    "rm -fr \"$_archive\"\n", sap->sa_dest);
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

struct build_copy_from *
build_stage_find_copy_from(int stage, build_copy_from_t *head)
{
	struct build_copy_from *cfp;

	TAILQ_FOREACH(cfp, head, glue) {
		if (cfp->stage != stage) {
			continue;
		}
		return (cfp);
	}
	return (NULL);
}

static int
build_stage_compile_copy_from(struct build_context *bcp, int stage_index)
{
	char tar_path[MAXPATHLEN], root_path[MAXPATHLEN], *src, **argv;
	extern struct global_params gcfg;
	build_copy_from_t stage_deps;
	struct build_copy_from *cfp;
	int k, this_stage, status;
	struct build_step *bsp;
	vec_t *vec;
	pid_t pid;

	TAILQ_INIT(&stage_deps);
	for (k = 0; k < bcp->pbc.p_nsteps; k++) {
		bsp = &bcp->steps[k];
		if (bsp->stage_index != stage_index) {
			continue;
		}
		if (bsp->step_op != STEP_COPY_FROM) {
			continue;
		}
		this_stage = bsp->step_data.step_copy_from.sc_stage;
		cfp = build_stage_find_copy_from(this_stage, &stage_deps);
		if (cfp == NULL) {
			cfp = calloc(1, sizeof(*cfp));
			if (cfp == NULL) {
				warn("allocation failure");
				return (-1);
			}
			cfp->stage = this_stage;
			cfp->pathv = vec_init(32);
			TAILQ_INSERT_HEAD(&stage_deps, cfp, glue);
		}
		src = bsp->step_data.step_copy_from.sc_source;
		if (*src == '/') {
			src++;
		}
		vec_append(cfp->pathv, src);
	}
	if (TAILQ_EMPTY(&stage_deps)) {
		return (0);
	}
	TAILQ_FOREACH(cfp, &stage_deps, glue) {
		snprintf(root_path, sizeof(root_path),
		    "%s/instances/%s/%d/root", gcfg.c_data_dir,
		    bcp->instance, cfp->stage);
		snprintf(tar_path, sizeof(tar_path),
		    "%s/instances/copy_from_%s_%d.tar",
                    gcfg.c_data_dir, bcp->instance, cfp->stage);
		/*
		 * NB: we have the data to figure out exactly how many
		 * Items we will need.
		 */
		vec = vec_init(128);
		vec_append(vec, "/usr/bin/tar");
		vec_append(vec, "-C");
		vec_append(vec, root_path);
		vec_append(vec, "-cf");
		vec_append(vec, tar_path);
		vec_merge(cfp->pathv, vec);
		vec_finalize(vec);
		pid = fork();
		if (pid == -1) {
			warn("fork failed");
			return (-1);
		}
		if (pid == 0) {
			argv = vec_return(vec);
			execve(*argv, argv, NULL);
			err(1, "execve failed");
		}
		vec_free(vec);
		waitpid_ignore_intr(pid, &status);
	}
	return (0);
}

static int
build_emit_shell_script(struct build_context *bcp, int stage_index)
{
	int steps, k, header, taken;
	char script[MAXPATHLEN];
	struct build_step *bsp;
	FILE *fp;

	(void) snprintf(script, sizeof(script), "%s.%d.sh",
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
			fprintf(fp, ". /tmp/cblock_build_variables.sh\n");
			fprintf(fp, "set -e\n");
			if (bcp->pbc.p_verbose > 0) {
				fprintf(fp, "set -x\n");
			}
			header = 1;
		}
		fprintf(fp, "echo -n \033[1m--\033[0m\n");
		fprintf(fp, "echo ' Step %d/%d : %s'\n",
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
			fprintf(fp, "cp -pr /tmp/stage%d/%s %s\n",
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
	extern struct global_params gcfg;
	vec_t *vec, *vec_env;
	int status;
	pid_t pid;

	(void) snprintf(script, sizeof(script),
	    "%s/lib/stage_bootstrap_build.sh", gcfg.c_data_dir);
	(void) snprintf(index, sizeof(index), "%d", stage->bs_index);
	(void) snprintf(context_archive, sizeof(context_archive),
	    "%s/instances/%s.tar.gz", gcfg.c_data_dir, bcp->instance);
	pid = fork();
	if (pid == -1) {
		err(1, "fork failed");
	}
	if (pid != 0) {
		waitpid_ignore_intr(pid, &status);
		return (status);
	}
	/*
	 * Redirect any messages from the build container bootstrap
	 * processes to the console.
	 */
	vec_env = vec_init(16);
	vec_append(vec_env, DEFAULT_PATH);
	char buf[128];
	sprintf(buf, "CBLOCK_FS=%s", gcfg.c_underlying_fs);
	vec_append(vec_env, buf);
	vec_finalize(vec_env);
	vec = vec_init(32);
	vec_append(vec, "/bin/sh");
	if (bcp->pbc.p_verbose > 0) {
		vec_append(vec, "-x");
	}
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
	execve(*argv, argv, vec_return(vec_env));
	vec_free(vec);
	err(1, "execv failed");
	/* NOTREACHED */
	return (-1);
}

static int
build_commit_image(struct build_context *bcp)
{
	char commit_cmd[128], **argv, s_index[32], nstages[32];
	extern struct global_params gcfg;
	char path[1024], *do_fim, buf[64];
	struct build_stage *bsp;
	int status, k, last;
	FILE *fp;
	vec_t *vec, *vec_env;
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
	 * Write out the OS release specification for the container if there is
	 * one. If not, the default OS of the host environment will be used.
	 */
	if (bcp->pbc.p_os_release[0] != '\0') {
		snprintf(path, sizeof(path), "%s/%d/OSRELEASE",
		    bcp->build_root, last);
		fp = fopen(path, "w+");
		if (fp == NULL) {
			err(1, "fopen(%s) failed", path);
		}
		fprintf(fp, "%s", bcp->pbc.p_os_release);
		fclose(fp);
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
		waitpid_ignore_intr(pid, &status);
		if (status != 0) {
			warnx("failed to commit image");
		}
		return (status);
	}
	dup2(bcp->peer_sock, STDOUT_FILENO);
	dup2(bcp->peer_sock, STDERR_FILENO);
	snprintf(commit_cmd, sizeof(commit_cmd), "%s/lib/stage_commit.sh",
	    gcfg.c_data_dir);
	snprintf(nstages, sizeof(nstages), "%d", bcp->pbc.p_nstages);
	snprintf(s_index, sizeof(s_index), "%d", last);
	do_fim = "OFF";
	vec_env = vec_init(16);
	sprintf(buf, "CBLOCK_FS=%s", gcfg.c_underlying_fs);
	vec_append(vec_env, buf);
	vec_finalize(vec_env);
	vec = vec_init(16);
	vec_append(vec, "/bin/sh");
	if (bcp->pbc.p_verbose > 0) {
		vec_append(vec, "-x");
	}
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
	vec_append(vec, bcp->pbc.p_tag);
	vec_finalize(vec);
	argv = vec_return(vec);
	execve(*argv, argv, vec_return(vec_env));
	err(1, "execve: image commit failed");
	/* NOT REACHED */
	return (1);
}

static int
build_run_build_stage(struct build_context *bcp)
{
	char stage_root[MAXPATHLEN], **argv, builder[1024], buf[512];
	extern struct global_params gcfg;
	struct build_stage *bstg;
	vec_t *vec, *vec_env;
	int status, k;
	pid_t pid;

	(void) snprintf(bcp->build_root, sizeof(bcp->build_root),
	    "%s/instances/%s", gcfg.c_data_dir, bcp->instance);
        sprintf(builder, "%s/lib/stage_build.sh", gcfg.c_data_dir);
	for (k = 0; k < bcp->pbc.p_nstages; k++) {
		bstg = &bcp->stages[k];
		snprintf(stage_root, sizeof(stage_root),
		    "%s/%d", bcp->build_root, bstg->bs_index);
		if (mkdir(stage_root, 0755) == -1) {
			err(1, "mkdir(%s) stage root", stage_root);
		}
		snprintf(stage_root, sizeof(stage_root),
		    "%s/%d/root", bcp->build_root, bstg->bs_index);
		if (mkdir(stage_root, 0755) == -1) {
			err(1, "mkdir(%s) stage root mount failed", stage_root);
		}
		build_emit_shell_script(bcp, bstg->bs_index);
		if (build_stage_compile_copy_from(bcp, bstg->bs_index)) { 
			printf("Stage has COPY FROM instruction\n");
		}
		status = build_init_stage(bcp, bstg);
		if (status != 0) {
			print_bold_prefix(stdout);
			fprintf(stdout,
			    "Stage index %d failed with %d code. Exiting\n",
                            bstg->bs_index,
			    WEXITSTATUS(status));
			fflush(stdout);
			break;
		}
		print_bold_prefix(stdout);
		if (bstg->bs_name[0] != '\0') {
			fprintf(stdout,
			    "Executing stage (%d/%d) : FROM %s AS %s\n",
			    k + 1, bcp->pbc.p_nstages, bstg->bs_base_container,
			    bstg->bs_name);
		} else {
			fprintf(stdout,
			    "Executing stage (%d/%d) : FROM %s\n",
			    k + 1, bcp->pbc.p_nstages,
			    bstg->bs_base_container);
		}
		fflush(stdout);
		snprintf(stage_root, sizeof(stage_root),
		    "%s/%d/root", bcp->build_root, bstg->bs_index);
		pid = fork();
		if (pid == -1) {
			err(1, "pid failed");
		}
		if (pid == 0) {
			/*
			 * We need to setup a functional environment here,
			 * especially for build containers. PATH is really
			 * important.
			 */
			sprintf(buf, "CBLOCK_FS=%s", gcfg.c_underlying_fs);
			vec_env = vec_init(8);
			vec_append(vec_env, buf);
			vec_append(vec_env, "USER=root");
			vec_append(vec_env, "HOME=/root");
			vec_append(vec_env, DEFAULT_PATH);
			vec_append(vec_env, "TERM=xterm");
			vec_append(vec_env, "BLOCKSIZE=K");
			vec_append(vec_env, "SHELL=/bin/sh");
			vec_finalize(vec_env);

			vec = vec_init(32);
			vec_append(vec, "/bin/sh");
			if (bcp->pbc.p_verbose > 0) {
				vec_append(vec, "-x");
			}
			vec_append(vec, builder);
			vec_append(vec, stage_root);
			vec_append(vec, bcp->instance);
			vec_append(vec, bcp->pbc.p_os_release);
			vec_finalize(vec);
			argv = vec_return(vec);
			execve(*argv, argv, vec_return(vec_env));
			err(1, "execve failed");
		}
		waitpid_ignore_intr(pid, &status);
		if (status != 0) {
			print_bold_prefix(stdout);
			fprintf(stdout,
			    "Execution of stage of %d ", k + 1);
			print_red(stdout, "failed");
			fprintf(stdout,
			    ". Terminating.\n");
			fflush(stdout);
			break;
		}
	}
	if (status == 0) {
		bstg = &bcp->stages[k - 1];
		bstg->bs_is_last = 1;
	}
	return (status);
}

static int
dispatch_build_set_outfile(struct build_context *bcp,
    char *ebuf, size_t len)
{
	extern struct global_params gcfg;
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

int
dispatch_build_recieve(int sock)
{
	extern cblock_instance_head_t pr_head;
	extern pthread_mutex_t cblock_mutex;
	struct cblock_instance *pi;

	struct cblock_response resp;
	struct build_context bctx;
	char *build_type;
	ssize_t cc;
	int fd;

	bzero(&bctx, sizeof(bctx));
	cc = sock_ipc_must_read(sock, &bctx.pbc, sizeof(bctx.pbc));
	if (cc == 0) {
		printf("didn't get proper build context headers\n");
		return (0);
	}
	build_type = "build";
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
	sock_ipc_must_read(sock, bctx.steps,
	    bctx.pbc.p_nsteps * sizeof(*bctx.steps));
	bctx.instance = gen_sha256_instance_id(bctx.pbc.p_image_name);
	fd = dispatch_build_set_outfile(&bctx, resp.p_errbuf,
	    sizeof(resp.p_errbuf));
	if (fd == -1) {
		warn("dispatch_build_set_outfile: failed");
		free(bctx.steps);
		free(bctx.stages);
		resp.p_ecode = -1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
        }
	if (sock_ipc_from_to(sock, fd, bctx.pbc.p_context_size) == -1) {
		free(bctx.steps);
		free(bctx.stages);
		close(fd);
		warn("sock_ipc_from_to failed");
		return (1);
	}
	close(fd);
	pi = calloc(1, sizeof(*pi));
	if (pi == NULL) {
		err(1, "calloc failed");
	}
	pi->p_type = PRISON_TYPE_BUILD;
	pi->p_instance_tag = strdup(bctx.instance); /* NB: check free */
	strlcpy(pi->p_image_name, bctx.pbc.p_image_name, sizeof(pi->p_image_name));
	pi->p_launch_time = time(NULL);
	pi->p_pid = forkpty(&pi->p_ttyfd, pi->p_ttyname, NULL, NULL);
	if (pi->p_pid == -1) {
		warn("failed to fork build job");
		return (1);
	}
	if (pi->p_pid > 0) {
		CBLOCKD_CBLOCK_CREATE(pi->p_instance_tag);
		TAILQ_INIT(&pi->p_ttybuf.t_head);
		cblock_create_pid_file(pi);
		pi->p_ttybuf.t_tot_len = 0;
		pthread_mutex_lock(&cblock_mutex);
		TAILQ_INSERT_HEAD(&pr_head, pi, p_glue);
		pthread_mutex_unlock(&cblock_mutex);
		snprintf(resp.p_errbuf, sizeof(resp.p_errbuf), "%s",
		    pi->p_instance_tag);
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	/*
	 * Child process, all stdout/stdin is routed to the PTY
	 */
	print_bold_prefix(stdout);
	printf("Bootstrapping build stages 1 through %d\n", bctx.pbc.p_nstages); 
	fflush(stdout);
	if (build_run_build_stage(&bctx) != 0) {
		fprintf(stdout, "build_run_build_stage failed\n");
		_exit(1);
	}
	print_bold_prefix(stdout);
	fprintf(stdout,
	    "Build Stage(s) complete. Writing container image...\n");
	fflush(stdout);
	if (build_commit_image(&bctx) != 0) {
		fprintf(stdout, "build_commit_image: failed\n");
		_exit(1);
	}
	print_bold_prefix(stdout);
	fprintf(stdout,
	    "Cleaning up ephemeral images and build artifacts\n");
	fflush(stdout);
	free(bctx.instance);
	_exit(0);
	/* NOT REACHED */
	return (1);
}
