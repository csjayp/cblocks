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
#include <termios.h>
#include <libutil.h>
#include <signal.h>
#include <string.h>

#include <libprison.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"
#include "config.h"

static int
dispatch_build_set_outfile(struct prison_build_context *pbp,
    char *ebuf, size_t len)
{
	char path[512], build_root[512];
	int fd;

	(void) snprintf(path, sizeof(path),
	    "%s/spool/%s-%s.tar.gz", gcfg.c_data_dir, pbp->p_image_name,
	    pbp->p_tag);
	fd = open(path, O_RDWR | O_EXCL | O_CREAT, 0600);
	if (fd == -1) {
		snprintf(ebuf, len, "could not write to build spool: %s",
		    strerror(errno));
		return (-1);
	}
	(void) snprintf(build_root, sizeof(build_root),
	    "%s/spool/%s-%s", gcfg.c_data_dir, pbp->p_image_name, pbp->p_tag);
	if (mkdir(build_root, 0755) == -1) {
		snprintf(ebuf, len, "failed to initialize build env: %s",
		    strerror(errno));
		(void) unlink(path);
		close(fd);
		return (-1);
	}
	return (fd);
}

static int
dispatch_process_stages(struct prison_build_context *bcp,
    struct build_stage *stages, struct build_step *steps)
{
	struct build_stage *sp;
	int index;

	for (index = 0; index < bcp->p_nstages; index++) {
		sp = &stages[index];
		printf("DEBUG: building %d from container %s\n",
		    sp->bs_index, sp->bs_base_container);
	}
	return (0);
}

int
dispatch_build_recieve(int sock)
{
	struct prison_build_context pbc;
	struct prison_response resp;
	struct build_stage *stages;
	struct build_step *steps;
	ssize_t cc;
	int fd;

	printf("executing build recieve\n");
	cc = sock_ipc_must_read(sock, &pbc, sizeof(pbc));
	if (cc == 0) {
		printf("didn't get proper build context headers\n");
		return (0);
	}
	if (pbc.p_nstages > MAX_BUILD_STAGES ||
	    pbc.p_nsteps > MAX_BUILD_STEPS) {
		resp.p_ecode = -1;
		sprintf(resp.p_errbuf, "too many build stages/steps\n");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	stages = calloc(pbc.p_nstages, sizeof(*stages));
	if (stages == NULL) {
		resp.p_ecode = -1;
		sprintf(resp.p_errbuf, "out of memory");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	steps = calloc(pbc.p_nsteps, sizeof(*steps));
	if (steps == NULL) {
		resp.p_ecode = -1;
		sprintf(resp.p_errbuf, "out of memory");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	sock_ipc_must_read(sock, stages, pbc.p_nstages * sizeof(*stages));
	printf("read %lu bytes of stages\n", pbc.p_nstages * sizeof(*stages));
	sock_ipc_must_read(sock, steps, pbc.p_nsteps * sizeof(*steps));
	printf("read %lu bytes of steps\n", pbc.p_nsteps * sizeof(*steps));
	fd = dispatch_build_set_outfile(&pbc, resp.p_errbuf,
	    sizeof(resp.p_errbuf));
	if (fd == -1) {
		free(steps);
		free(stages);
		resp.p_ecode = -1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
        }
	if (sock_ipc_from_to(sock, fd, pbc.p_context_size) == -1) {
		err(1, "sock_ipc_from_to failed");
	}
	bzero(&resp, sizeof(resp));
	resp.p_ecode = 0;
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	dispatch_process_stages(&pbc, stages, steps);
	close(fd);
	return (1);
}
