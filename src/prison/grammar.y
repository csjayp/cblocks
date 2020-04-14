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
#include <stdio.h>
#include <stdint.h>

#include "parser.h"
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
	;

op_spec:
	RUN STRING
	{
		printf("\texecuting '%s'\n", $2);
	}
	| ADD STRING STRING
	{
		printf("\tAdding file/tar/linked file refereced by %s to %s in container\n",
		    $2, $3);
	}
	| COPY COPY_FROM STRING STRING STRING
	{
		printf("\tCopy file (%s) from stage %s to %s\n", $4, $3, $5);
	}
	| COPY COPY_FROM INTEGER STRING STRING
	{
		printf("\tCopy file (%s) from stage %d to %s\n", $4, $3, $5);
	}
	| COPY STRING STRING
	{
		printf("\tCopying local file %s to %s in container\n", $2, $3);
	}
	| WORKDIR STRING
	{
		printf("\tsetting work directory to %s\n", $2);
	}
	| ENTRYPOINT OPEN_SQUARE_BRACKET STRING CLOSE_SQUARE_BRACKET
	{
		printf("\tSeting container entry point to %s\n", $3);
	}
	;

operations : /* empty */
	| operations op_spec
	;

stage_def:
	FROM STRING operations
	{
		printf("--- stage committed: base image %s\n", $2);
	}
	| FROM STRING AS STRING operations
	{
		printf("--- stage %s committed: base image %s\n", $4, $2);
	}
	;
