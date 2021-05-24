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
#include <ctype.h>
#include <paths.h>
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
#include <assert.h>
#include <string.h>

#include <openssl/sha.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"
#include "cblock.h"
#include "config.h"

#include "probes.h"

#include <cblock/libcblock.h>

static int reap_children;
cblock_peer_head_t p_head;
cblock_instance_head_t pr_head;
pthread_mutex_t peer_mutex;
pthread_mutex_t cblock_mutex;

int
cblock_create_pid_file(struct cblock_instance *p)
{
	extern struct global_params gcfg;
	char pid_path[1024], pid_buf[32];
	mode_t mode;
	int flags;

	mode = S_IRUSR | S_IWUSR;
	flags = O_WRONLY | O_EXCL | O_EXLOCK | O_CREAT;
	snprintf(pid_path, sizeof(pid_path), "%s/locks/%s.pid",
	    gcfg.c_data_dir, p->p_instance_tag);
	snprintf(pid_buf, sizeof(pid_buf), "%d", p->p_pid);
	p->p_pid_file = open(pid_path, flags, mode);
	assert(p->p_pid_file != 0);
	if (p->p_pid_file == -1) {
		warn("open(%s)", pid_path);
		return (-1);
	}
	if (write(p->p_pid_file, pid_buf, strlen(pid_buf)) == -1) {
		warn("write pid file failed");
		return (-1);
	}
	return (0);
}

size_t
cblock_instance_get_count(void)
{
	struct cblock_instance *p;
	size_t count;

	if (TAILQ_EMPTY(&pr_head)) {
		return (0);
	}
	count = 0;
	pthread_mutex_lock(&cblock_mutex);
	TAILQ_FOREACH(p, &pr_head, p_glue) {
		count++;
	}
	pthread_mutex_unlock(&cblock_mutex);
	return (count);
}

struct instance_ent *
cblock_populate_instance_entries(size_t max_ents)
{
	struct instance_ent *vec, *cur;
	struct cblock_instance *p;
	int counter;

	vec = calloc(max_ents, sizeof(*vec));
	if (vec == NULL) {
		return (NULL);
	}
	counter = 0;
	pthread_mutex_lock(&cblock_mutex);
	TAILQ_FOREACH(p, &pr_head, p_glue) {
		cur = &vec[counter];
		strlcpy(cur->p_instance_name, p->p_instance_tag,
		    sizeof(cur->p_instance_name));
		strlcpy(cur->p_image_name, p->p_image_name,
		    sizeof(cur->p_image_name));
		cur->p_pid = p->p_pid;
		strlcpy(cur->p_tty_line, p->p_ttyname,
		    sizeof(cur->p_tty_line));
		cur->p_start_time = p->p_launch_time;
		if (counter == max_ents) {
			break;
		}
		counter++;
	}
	pthread_mutex_unlock(&cblock_mutex);
	return (vec);
}

int
cblock_instance_match(char *full_instance_name, const char *user_supplied)
{
	size_t slen;

	slen = strlen(user_supplied);
	if (slen == 10) {
		if (strncmp(full_instance_name, user_supplied, 10) == 0) {
			return (1);
		}
		return (0);
	}
	return (strcmp(full_instance_name, user_supplied) == 0);
}

void
cblock_fork_cleanup(char *instance, char *type, int dup_sock, int verbose)
{
	extern struct global_params gcfg;
        char buf[128], **argv;
        vec_t *vec, *vec_env;
        int status;

        pid_t pid = fork();
        if (pid == -1) {
                err(1, "cblock_remove: failed to execute cleanup handlers");
        }
        if (pid == 0) {
		vec_env = vec_init(8);
		sprintf(buf, "CBLOCK_FS=%s", gcfg.c_underlying_fs);
		vec_append(vec_env, buf);
		vec_finalize(vec_env);

		vec = vec_init(16);
		vec_append(vec, "/bin/sh");
		if (verbose > 0) {
			vec_append(vec, "-x");
		}
		sprintf(buf, "%s/lib/stage_launch_cleanup.sh", gcfg.c_data_dir);
		vec_append(vec, buf);
		vec_append(vec, gcfg.c_data_dir);
		vec_append(vec, instance);
		sprintf(buf, "%s", type);
		vec_append(vec, buf);
		vec_finalize(vec);
		argv = vec_return(vec);
		if (dup_sock >= 0) {
			dup2(dup_sock, STDOUT_FILENO);
			dup2(dup_sock, STDERR_FILENO);
		}
		execve(*argv, argv, vec_return(vec_env));
		err(1, "cblock_remove: execve failed");
	}
	waitpid_ignore_intr(pid, &status);
}

void
cblock_remove(struct cblock_instance *pi)
{
	extern struct global_params gcfg;
	uint32_t cmd;
	size_t cur;

	/*
	 * Tell the remote side to dis-connect.
	 *
	 * NB: we are holding a lock here. We need to re-factor this a bit
	 * so we aren't performing socket io while this lock is held.
	 */
	CBLOCKD_CBLOCK_DESTROY(pi->p_instance_tag);
	if ((pi->p_state & STATE_CONNECTED) != 0) {
		cmd = PRISON_IPC_CONSOLE_SESSION_DONE;
		sock_ipc_must_write(pi->p_peer_sock, &cmd, sizeof(cmd));
		/*
		 * If this is a cellblock build, the peer will be waiting for
		 * ultimate status code of the build job, so send it.
		 */
		if (pi->p_type == PRISON_TYPE_BUILD) {
			printf("reporting error code to peer %d\n", pi->p_status);
			sock_ipc_must_write(pi->p_peer_sock, &pi->p_status,
			    sizeof(pi->p_status));
		}
	}
	cblock_fork_cleanup(pi->p_instance_tag, "regular", -1, gcfg.c_verbose);
	assert(pi->p_ttyfd != 0);
	(void) close(pi->p_ttyfd);
	TAILQ_REMOVE(&pr_head, pi, p_glue);
	cur = pi->p_ttybuf.t_tot_len;
	while (cur > 0) {
		cur = termbuf_remove_oldest(&pi->p_ttybuf);
	}
	assert(pi->p_pid_file != 0);
	close(pi->p_pid_file);
	free(pi);
}

void
cblock_detach_console(const char *instance)
{
	struct cblock_instance *pi;

	pthread_mutex_lock(&cblock_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (!cblock_instance_match(pi->p_instance_tag, instance)) {
			continue;
		}
		pi->p_state &= ~STATE_CONNECTED;
		pi->p_peer_sock = -1;
		pthread_mutex_unlock(&cblock_mutex);
		return;
	}
	pthread_mutex_unlock(&cblock_mutex);
	/*
	 * If we are here, the process was non-interactive (build job) and
	 * has completed already.
	 */
}

void
cblock_reap_children(void)
{
	struct cblock_instance *pi, *p_temp;
	int status;
	pid_t pid;

	pthread_mutex_lock(&cblock_mutex);
	TAILQ_FOREACH_SAFE(pi, &pr_head, p_glue, p_temp) {
		pid = waitpid(pi->p_pid, &status, WNOHANG);
		if (pid != pi->p_pid) {
			continue;
		}
		pi->p_state |= STATE_DEAD;
		pi->p_status = status;
		cblock_remove(pi);
	}
	pthread_mutex_unlock(&cblock_mutex);
	reap_children = 0;
}

int
cblock_instance_is_dead(const char *instance)
{
	struct cblock_instance *pi;
	int isdead;

	isdead = 0;
	pthread_mutex_lock(&cblock_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (!cblock_instance_match(pi->p_instance_tag, instance)) {
			continue;
		}
		isdead = ((pi->p_state & STATE_DEAD) != 0);
		pthread_mutex_unlock(&cblock_mutex);
		return (isdead);
        }
        pthread_mutex_unlock(&cblock_mutex);
	/*
	 * The console was non-interactive (i.e.: a build job) and it is
	 * complete and the process(s) have been reaped. We might want
	 * to assert that this is the case some place.
	 */
        return (1);
}

struct cblock_instance *
cblock_lookup_instance(const char *instance)
{
	struct cblock_instance *pi;

	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (!cblock_instance_match(pi->p_instance_tag, instance)) {
			continue;
		}
		return (pi);
	}
	return (NULL);
}

void *
cblock_handle_request(void *arg)
{
	struct cblock_peer *p;

	p = (struct cblock_peer *)arg;
	if (pthread_create(&p->p_thr, NULL, dispatch_work, arg) != 0) {
		err(1, "pthread_create(dispatch_work) failed");
	}
	pthread_mutex_lock(&peer_mutex);
	TAILQ_INSERT_HEAD(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
	return (NULL);
}
