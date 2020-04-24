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
#ifndef LIBPRISON_DOT_H_
#define	LIBPRISON_DOT_H_
#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/queue.h>
#include <sys/ttycom.h>

#include <termios.h>

struct tailhead_stage;
struct tailhead_step;

int		sock_ipc_may_read(int, void *, size_t);
ssize_t		sock_ipc_must_read(int, void *, size_t);
ssize_t		sock_ipc_must_write(int, void *, size_t);
ssize_t		sock_ipc_from_to(int, int, off_t);

#define	MAX_PRISON_NAME	512

#define	PRISON_IPC_LAUNCH_PRISON	1
#define	PRISON_IPC_CONSOLE_CONNECT	2
#define	PRISON_IPC_CONSOLE_DATA		3
#define PRISON_IPC_CONSOL_RESIZE	4
#define	PRISON_IPC_SEND_BUILD_CTX	5

struct prison_build_context {
	char		p_image_name[1024];
	char		p_prison_file[1024];
	off_t		p_context_size;
	char		p_tag[1024];
	int		p_nstages;
	int		p_nsteps;
};

struct prison_response {
	int		p_ecode;
	char		p_errbuf[512];
};

struct prison_launch {
	char		p_name[MAX_PRISON_NAME];
	char		p_term[128];
};

struct prison_console_connect {
	char		p_name[MAX_PRISON_NAME];
	struct winsize 	p_winsize;
	struct termios	p_termios;
	char		p_term[64];
};

/*
 * Data structures to facilitate image builds, shared between the client
 * and daemon processs.
 */
struct build_step_workdir {
	char					sw_dir[MAXPATHLEN];
};

struct build_step_add {
	int					 sa_op;
#define	ADD_TYPE_FILE		1
#define	ADD_TYPE_ARCHIVE	2
#define	ADD_TYPE_URL		3
	char					sa_source[MAXPATHLEN];
	char					sa_dest[MAXPATHLEN];
};

struct build_step_copy {
	char					sc_source[MAXPATHLEN];
	char					sc_dest[MAXPATHLEN];
};

struct build_step {
	int					step_op;
	int					stage_index;
#define	STEP_ADD	1
#define	STEP_COPY	2
#define	STEP_RUN	3
#define	STEP_WORKDIR	4
	TAILQ_ENTRY(build_step)	step_glue;
	union {
		/*
		 * NB: This should be dynamic, following something like
		 * getcnf(ARG_MAX) but we will re-visit this in the
		 * future if need be.
		 */
		char				 step_cmd[2048];
		struct build_step_copy		 step_copy;
		struct build_step_add		 step_add;
		struct build_step_workdir	 step_workdir;
	} step_data;
};

struct build_stage {
	char					 bs_name[1024];
	int					 bs_index;
	char					 bs_base_container[1024];
	TAILQ_HEAD(tailhead_step, build_step)	step_head;
	TAILQ_ENTRY(build_stage)		stage_glue;
};

struct build_manifest {
	TAILQ_HEAD(tailhead_stage, build_stage)	 stage_head;
	char					*entry_point;
	char					*maintainr;
};

#endif	/* BUILD_DOT_H_ */
