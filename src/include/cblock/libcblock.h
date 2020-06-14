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

#define	MAX_PRISON_NAME	512

enum {
        PRISON_TYPE_NONE,
        PRISON_TYPE_BUILD,
        PRISON_TYPE_REGULAR
};

#define	PRISON_IPC_LAUNCH_PRISON	1
#define	PRISON_IPC_CONSOLE_CONNECT	2
#define	PRISON_IPC_CONSOLE_DATA		3
#define PRISON_IPC_CONSOL_RESIZE	4
#define	PRISON_IPC_SEND_BUILD_CTX	5
#define	PRISON_IPC_LAUNCH_BUILD		6
#define	PRISON_IPC_CONSOLE_TO_CLIENT	7
#define	PRISON_IPC_CONSOLE_SESSION_DONE	8
#define	PRISON_IPC_GET_INSTANCES	9
#define	PRISON_IPC_GENERIC_COMMAND	10
#define	PRISON_IPC_NETWORK_CTL		11

struct instance_ent {
	char					p_instance_name[512];
	char					p_image_name[512];
	pid_t					p_pid;
	char					p_tty_line[64];
	time_t					p_start_time;
};

struct cblock_generic_command {
	char					p_cmdname[512];
	size_t					p_mlen;
};

struct cblock_build_context {
	char					p_image_name[1024];
	char					p_cblock_file[1024];
	off_t					p_context_size;
	char					p_tag[1024];
	int					p_nstages;
	int					p_nsteps;
	char					p_term[128];
	char					p_entry_point[1024];
	char					p_entry_point_args[1024];
	int					p_verbose;
	int					p_build_fim_spec;
};

struct cblock_response {
	int					p_ecode;
	char					p_errbuf[1024];
};

struct cblock_launch {
	char					p_name[MAX_PRISON_NAME];
	char					p_tag[1024];
	char					p_term[128];
	char					p_entry_point_args[1024];
	char					p_volumes[2048];
	char					p_ports[2048];
	char					p_network[256];
	int					p_verbose;
};

struct cblock_console_connect {
	char					p_name[MAX_PRISON_NAME];
	char					p_instance[MAX_PRISON_NAME];
	struct winsize				p_winsize;
	struct termios				p_termios;
	char					p_term[64];
};

struct build_step_root_pivot {
	char					sr_dir[MAXPATHLEN];
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
#define	ADD_TYPE_ARCHIVE_URL	4
	char					sa_source[MAXPATHLEN];
	char					sa_dest[MAXPATHLEN];
};

struct build_step_copy_from {
	int					sc_stage;
	char					sc_source[MAXPATHLEN];
	char					sc_dest[MAXPATHLEN];
};

struct build_step_env {
	char					se_key[MAXPATHLEN];
	char					se_value[MAXPATHLEN];
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
#define	STEP_COPY_FROM	5
#define	STEP_ROOT_PIVOT	6
#define	STEP_ENV	7
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
		struct build_step_copy_from	 step_copy_from;
		struct build_step_root_pivot	 step_root_pivot;
		struct build_step_env		 step_env;
	} step_data;
	char					 step_string[1024];
};

struct build_stage {
	char					 bs_name[1024];
	int					 bs_index;
	char					 bs_base_container[1024];
	TAILQ_HEAD(tailhead_step, build_step)	step_head;
	TAILQ_ENTRY(build_stage)		stage_glue;
	int					 bs_is_last;
};

struct build_manifest {
	TAILQ_HEAD(tailhead_stage, build_stage)	 stage_head;
	char					*entry_point;
	char					*entry_point_args;
	char					*maintainr;
};

struct build_context {
	struct cblock_build_context		pbc;
	struct build_step			*steps;
	struct build_stage			*stages;
	char					 build_root[MAXPATHLEN];
	TAILQ_ENTRY(build_context)		bc_glue;
	char					*instance;
	int					peer_sock;
	FILE					*peer_sock_fp;
};

struct vec {
        char **					vec;
        size_t					vec_used;
        size_t					vec_alloc;
#define VEC_OVERFLOW	1
#define VEC_ENOMEM	2
        int					vec_flag;
	char *					vec_marshalled;
	size_t					vec_marshalled_len;
};

typedef struct vec vec_t;

void		print_red(FILE *, char *, ...);
void		print_bold_prefix(FILE *);
pid_t		waitpid_ignore_intr(pid_t, int *);
vec_t *		vec_init(size_t);
void		vec_append(vec_t *, char *);
int		vec_finalize(vec_t *);
char **		vec_return(vec_t *);
void		vec_free(vec_t *);
char *		vec_join(vec_t *, char);
char **		vec_unmarshal(vec_t *, char *, size_t);
char *		vec_marshal(vec_t *);
int		vec_merge(vec_t *, vec_t *);
int		sock_ipc_may_read(int, void *, size_t);
ssize_t		sock_ipc_must_read(int, void *, size_t);
ssize_t		sock_ipc_must_write(int, void *, size_t);
ssize_t		sock_ipc_from_to(int, int, off_t);
void		sock_ipc_from_sock_to_tty(int);

#endif	/* BUILD_DOT_H_ */
