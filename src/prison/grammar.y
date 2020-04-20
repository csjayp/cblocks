%{
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

#include <stdio.h>
#include <stdint.h>
#include <fnmatch.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <assert.h>

#include <libprison.h>

#include "parser.h"

static struct build_manifest	*cur_build_manifest;
static struct build_stage	*cur_build_stage;
static struct build_step	*cur_build_step;
static int stage_counter;

static char *archive_extensions[] = {
	"*.tar.gz",
	"*.tgz",
	"*.txz",
	"*.tar.xz",
	"*.tar.bz2",
	"*.tbz2",
	NULL
};

%}

%union {
	uint32_t	 num;
        char		*c_string;
}

%token FROM AS COPY ADD RUN ENTRYPOINT STRING WORKDIR
%token OPEN_SQUARE_BRACKET CLOSE_SQUARE_BRACKET COPY_FROM
%token INTEGER

%type <num> INTEGER
%type <c_string> STRING

%%

root	: /* empty */
	| root stage
	;

stage	:
	stage_def
	| entry_def
	;

entry_def:
	ENTRYPOINT OPEN_SQUARE_BRACKET STRING CLOSE_SQUARE_BRACKET
        {
		struct build_manifest *bmp;

		bmp = get_current_build_manifest();
		if (bmp->entry_point != NULL) {
			errx(1, "ENTRPOINT: only one entry point per build specification");
		}
		bmp->entry_point = strdup($3);
		if (bmp->entry_point == NULL) {
			err(1, "strdup: entrypoint failed");
		}
        }
        ;

copy_spec:
	COPY_FROM STRING STRING STRING
	| COPY_FROM INTEGER STRING STRING
	| STRING STRING
	{
		struct build_step *b_step;
		struct build_stage *bsp;

		assert(cur_build_step != NULL);
		assert(cur_build_stage != NULL);
		bsp = cur_build_stage;
		b_step = cur_build_step;
		b_step->step_data.step_copy.sc_source = strdup($1);
		if (!b_step->step_data.step_copy.sc_source) {
			err(1, "strdup failed");
		}
		b_step->step_data.step_copy.sc_dest = strdup($2);
		if (!b_step->step_data.step_copy.sc_dest) {
			err(1, "strdup failed");
		}
		cur_build_step->stage_index = cur_build_stage->bs_index;
		TAILQ_INSERT_HEAD(&bsp->step_head, b_step, step_glue);
		cur_build_step = NULL;
	}
	;

op_spec:
	RUN
	{
		struct build_step *b_step;

		b_step = calloc(1, sizeof(*b_step));
		if (b_step == NULL) {
			err(1, "calloc(build step) faild");
		}
		b_step->step_op = STEP_RUN;
		cur_build_step = b_step;
	}
	STRING
	{
		struct build_step *b_step;
		struct build_stage *bsp;

		assert(cur_build_step != NULL);
		assert(cur_build_stage != NULL);
		bsp = cur_build_stage;
		b_step = cur_build_step;
		b_step->step_data.step_cmd = strdup($3);
		if (b_step->step_data.step_cmd == NULL) {
			err(1, "calloc(run command) failed");
		}
		cur_build_step->stage_index = cur_build_stage->bs_index;
		TAILQ_INSERT_HEAD(&bsp->step_head, b_step, step_glue);
		cur_build_step = NULL;
	}
	| ADD
	{
		struct build_step *b_step;

		b_step = calloc(1, sizeof(*b_step));
		if (b_step == NULL) {
			err(1, "calloc(build step) faild");
		}
		b_step->step_op = STEP_ADD;
		cur_build_step = b_step;
	}
	STRING STRING
	{
		char **pattern_list, *pat;
		struct build_step *b_step;
		struct build_stage *bsp;
		int match;

		bsp = cur_build_stage;
		b_step = cur_build_step;
		b_step->step_data.step_add.sa_source = $3;
		b_step->step_data.step_add.sa_dest = $4;
		if (strncasecmp("http://", $3, 7) == 0) {
			b_step->step_data.step_add.sa_op = ADD_TYPE_URL;
		} else if (strncasecmp("https://", $3, 8) == 0) {
			b_step->step_data.step_add.sa_op = ADD_TYPE_URL;
		}
		pattern_list = archive_extensions;
		match = 0;
		while ((pat = *pattern_list++)) {
			if (!fnmatch(pat, $3, FNM_CASEFOLD)) {
				match = 1;
				break;
			}
		}
		if (match) {
			b_step->step_data.step_add.sa_op = ADD_TYPE_ARCHIVE;
		}
		cur_build_step->stage_index = cur_build_stage->bs_index;
		TAILQ_INSERT_HEAD(&bsp->step_head, b_step, step_glue);
		cur_build_step = NULL;
	}
	| COPY
	{
		struct build_step *b_step;

		b_step = calloc(1, sizeof(*b_step));
		if (b_step == NULL) {
			err(1, "calloc(build step) faild");
		}
		b_step->step_op = STEP_COPY;
		cur_build_step = b_step;
	} copy_spec
	| WORKDIR
	{
		struct build_step *b_step;

		b_step = calloc(1, sizeof(*b_step));
		if (b_step == NULL) {
			err(1, "calloc(build step) faild");
		}
		b_step->step_op = STEP_WORKDIR;
		cur_build_step = b_step;

	}
	STRING
	{
		struct build_step *b_step;
		struct build_stage *bsp;

		b_step = cur_build_step;
		bsp = cur_build_stage;
		assert(b_step != NULL);
		assert(bsp != NULL);
		b_step->step_data.step_workdir.sw_dir = strdup($3);
		if (!b_step->step_data.step_workdir.sw_dir) {
			err(1, "strdup failed");
		}
		cur_build_step->stage_index = cur_build_stage->bs_index;
		TAILQ_INSERT_HEAD(&bsp->step_head, b_step, step_glue);
		cur_build_step = NULL;
	}
	;

operations : /* empty */
	| operations op_spec
	;

from_spec:
	| STRING
	{
		struct build_stage *bsp;

		bsp = cur_build_stage;
		assert(bsp != NULL);
		bsp->bs_base_container = strdup($1);
		if (!bsp->bs_base_container) {
			err(1, "faild to copy base container name");
		}
	}
	| STRING AS STRING
	{
		struct build_stage *bsp;

		bsp = cur_build_stage;
		assert(bsp != NULL);
		printf("DEBUG: %s %s\n", $3, $1);
		bsp->bs_name = strdup($3);
		bsp->bs_base_container = strdup($1);
	}
	;

stage_def:
	FROM
	{
		struct build_stage *bsp;

		bsp = calloc(1, sizeof(*bsp));
		if (bsp == NULL) {
			err(1, "calloc(build stage) failed");
		}
		cur_build_stage = bsp;
	}
	from_spec operations
	{
		struct build_manifest *bmp;
		struct build_stage *bsp;

		bmp = get_current_build_manifest();
		bsp = cur_build_stage;
		bsp->bs_index = stage_counter++;
		printf("Inserted Stage: %s %p\n", bsp->bs_base_container, bsp);
		TAILQ_INSERT_HEAD(&bmp->stage_head, bsp, stage_glue);
		cur_build_stage = NULL;
	}
	;
%%

struct build_manifest *
build_manifest_init(void)
{
	struct build_manifest *bmp;

	bmp = calloc(1, sizeof(*bmp));
	if (bmp == NULL) {
		err(1, "calloc(build_manifest_init) failed");
	}
	TAILQ_INIT(&bmp->stage_head);
	return (bmp);
}

void
set_current_build_manifest(struct build_manifest *bmp)
{

	cur_build_manifest = bmp;
}

struct build_manifest *
get_current_build_manifest(void)
{

	assert(cur_build_manifest != NULL);
	return (cur_build_manifest);
}
