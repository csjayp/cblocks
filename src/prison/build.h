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
#ifndef BUILD_DOT_H_
#define BUILD_DOT_H_

struct build_step_workdir {
	char					*sw_dir;
};

struct build_step_add {
	int					 sa_op;
#define	ADD_TYPE_FILE		1
#define	ADD_TYPE_ARCHIVE	2
#define	ADD_TYPE_URL		3
	char					*sa_source;
	char					*sa_dest;
};

struct build_step_copy {
	char					*sc_source;
	char					*sc_dest;
};

struct build_step {
	int					step_op;
#define	STEP_ADD	1
#define	STEP_COPY	2
#define	STEP_RUN	3
#define	STEP_WORKDIR	4
	TAILQ_ENTRY(build_step)	step_glue;
	union {
		char				*step_cmd;
		struct build_step_copy		 step_copy;
		struct build_step_add		 step_add;
		struct build_step_workdir	 step_workdir;
	} step_data;
};

struct build_stage {
	char					*bs_name;
	int					 bs_index;
	char					*bs_base_container;
	TAILQ_HEAD( , build_step)		step_head;
	TAILQ_ENTRY(build_stage)		stage_glue;
};

struct build_manifest {
	TAILQ_HEAD( , build_stage)		 stage_head;
	char					*entry_point;
	char					*maintainr;
};

#endif	/* BUILD_DOT_H_ */
