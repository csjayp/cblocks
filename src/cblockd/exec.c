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
#include <sys/wait.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"
#include "config.h"

#include <cblock/libcblock.h>

struct command_ent {
	char	*command;
	char	*script_name;
};

static struct command_ent command_list[] = {
	{ "instance_prune",	"cmd_prune.sh" },
	{ "network-create",	"network_create.sh" },
	{ "image_list",		"cmd_image.sh" },
	{ NULL,			NULL }
};

static char *
lookup_script(char *command)
{
	struct command_ent *p;

	for (p = command_list; p->command != NULL; p++) {
		if (strcmp(p->command, command) == 0) {
			return (p->script_name);
		}
	}
	return (NULL);
}

int
dispatch_generic_command(int sock)
{
	struct cblock_generic_command arg;
	char *marshalled,*script;
	int pipefds[2], error;
	vec_t *vec;
	pid_t pid;

	/*
	 * The un-marshalling operation will initializ the vector. Use
	 * 0 here.
	 */
	vec = vec_init(0);
	marshalled = NULL;
	sock_ipc_must_read(sock, &arg, sizeof(arg));
	printf("got command %s\n", arg.p_cmdname);
	if (arg.p_mlen != 0) {
		marshalled = malloc(arg.p_mlen);
		if (marshalled == NULL) {
			return (1);
		}
		sock_ipc_must_read(sock, marshalled, arg.p_mlen);
		printf("read marshalled data\n");
		vec_unmarshal(vec, marshalled, arg.p_mlen);
		vec_finalize(vec);
	}
	script = lookup_script(arg.p_cmdname);
	if (script == NULL) {
		warnx("invalid command");
		return (1);
	}
	if (pipe2(pipefds, O_CLOEXEC) == -1) {
		warn("pipe2 failed");
		return (1); 
	}
	pid = fork();
	if (pid == -1) {
		warn("fork failed");
		return (1);
	}
	if (pid == 0) {
		char script_path[1024], **argv;
		vec_t *cmd_vec, *vec_env;
		int e;

		close(pipefds[0]);
		sprintf(script_path, "%s/lib/%s", gcfg.c_data_dir, script);
		cmd_vec = vec_init(64);
		vec_append(cmd_vec, "/bin/sh");
		vec_append(cmd_vec, script_path);
		vec_append(cmd_vec, "-R");
		vec_append(cmd_vec, gcfg.c_data_dir);
		if (vec_merge(vec, cmd_vec) != 0) {
			err(1, "error argv vectors: HINT: increase cmd_vec");
		}
		vec_finalize(cmd_vec);
		vec_env = vec_init(16);
		vec_append(vec_env, DEFAULT_PATH);
		char buf[128];
		sprintf(buf, "CBLOCK_FS=%s", gcfg.c_underlying_fs);
		vec_append(vec_env, buf);
		vec_finalize(vec_env);
		argv = vec_return(cmd_vec);
		dup2(sock, STDERR_FILENO);
		dup2(sock, STDOUT_FILENO);
		execve(*argv, argv, vec_return(vec_env));
		e = errno;
		write(pipefds[1], &e, sizeof(e));
		_exit(1);
	}
	close(pipefds[1]);
	while (1) {
		ssize_t cc;

		cc = read(pipefds[0], &error, sizeof(error));
		if (cc == 0) {
			break;
		}
		if (cc == -1 && errno == EINTR) {
			continue;
		}
		if (cc == -1) {
			warn("read (pipe) failed");
			return (1);
		}
		warn("execve failed %d", error);
		break;
	}
	while (1) {
		error = waitpid(pid, NULL, 0);
		if (error == pid) {
			break;
		}
		if (error == -1 && errno == EINTR) {
			continue;
		}
		if (error == -1) {
			warn("waitpid failed");
			return (1);
		}
	}
	vec_free(vec);
	return (1);
}
