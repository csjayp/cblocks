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
#include "config.h"

#include <libprison.h>

TAILQ_HEAD( , prison_peer) p_head;
TAILQ_HEAD( , prison_instance) pr_head;

static int reap_children;

pthread_mutex_t peer_mutex;
pthread_mutex_t prison_mutex;

#if 0
static void
tty_set_noecho(int fd)
{
	struct termios term;

	if (tcgetattr(fd, &term) == -1) {
		err(1, "tcgetattr: failed");
	}
	term.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
	term.c_oflag &= ~(ONLCR);
	if (tcsetattr(fd, TCSANOW, &term) == -1) {
		err(1, "tcsetattr: failed");
	}
}
#endif

int
prison_create_pid_file(struct prison_instance *p)
{
	char pid_path[1024], pid_buf[32];
	mode_t mode;
	int flags;

	mode = S_IRUSR | S_IWUSR;
	flags = O_WRONLY | O_EXCL | O_EXLOCK | O_CREAT;
	snprintf(pid_path, sizeof(pid_path), "%s/locks/%s.pid",
	    gcfg.c_data_dir, p->p_instance_tag);
	snprintf(pid_buf, sizeof(pid_buf), "%d", p->p_pid);
	p->p_pid_file = open(pid_path, flags, mode);
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
prison_instance_get_count(void)
{
	struct prison_instance *p;
	size_t count;

	if (TAILQ_EMPTY(&pr_head)) {
		return (0);
	}
	count = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(p, &pr_head, p_glue) {
		count++;
	}
	pthread_mutex_unlock(&prison_mutex);
	return (count);
}

struct instance_ent *
prison_populate_instance_entries(size_t max_ents)
{
	struct prison_instance *p;
	struct instance_ent *vec, *cur;
	int counter;

	vec = calloc(max_ents, sizeof(*vec));
	if (vec == NULL) {
		return (NULL);
	}
	counter = 0;
	pthread_mutex_lock(&prison_mutex);
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
	pthread_mutex_unlock(&prison_mutex);
	return (vec);
}

static int
prison_instance_match(char *full_instance_name, const char *user_supplied)
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

static void
handle_reap_children(int sig)
{

	reap_children = 1;
}

void
prison_fork_cleanup(char *instance, char *type, int dup_sock, int verbose)
{
        char buf[128], **argv;
        int status, ret;
        vec_t *vec, *vec_env;

        pid_t pid = fork();
        if (pid == -1) {
                err(1, "prison_remove: failed to execute cleanup handlers");
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
                err(1, "prison_remove: execve failed");
        }
        while (1) {
                ret = waitpid(pid, &status, 0);
                if (ret == -1 && errno == EINTR) {
                        continue;
                }
                if (ret == -1) {
                        err(1, "waitpid failed");
                }
                break;
        }
}

void
prison_remove(struct prison_instance *pi)
{
	uint32_t cmd;
	size_t cur;

	/*
	 * Tell the remote side to dis-connect.
	 *
	 * NB: we are holding a lock here. We need to re-factor this a bit
	 * so we aren't performing socket io while this lock is held.
	 */
	if ((pi->p_state & STATE_CONNECTED) != 0) {
		cmd = PRISON_IPC_CONSOLE_SESSION_DONE;
		sock_ipc_must_write(pi->p_peer_sock, &cmd, sizeof(cmd));
	}
	prison_fork_cleanup(pi->p_instance_tag, "regular", -1, 0);
	assert(pi->p_ttyfd != 0);
	(void) close(pi->p_ttyfd);
	TAILQ_REMOVE(&pr_head, pi, p_glue);
	cur = pi->p_ttybuf.t_tot_len;
	while (cur > 0) {
		cur = termbuf_remove_oldest(&pi->p_ttybuf);
	}
	close(pi->p_pid_file);
	free(pi);
}

static void
prison_detach_console(const char *instance)
{
	struct prison_instance *pi;

	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (!prison_instance_match(pi->p_instance_tag, instance)) {
			continue;
		}
		pi->p_state &= ~STATE_CONNECTED;
		pi->p_peer_sock = -1;
		pthread_mutex_unlock(&prison_mutex);
		return;
	}
	pthread_mutex_unlock(&prison_mutex);
	/*
	 * If we are here, the process was non-interactive (build job) and
	 * has completed already.
	 */
}

static void
prison_reap_children(void)
{
	struct prison_instance *pi, *p_temp;
	int status;
	pid_t pid;

	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH_SAFE(pi, &pr_head, p_glue, p_temp) {
		pid = waitpid(pi->p_pid, &status, WNOHANG);
		if (pid != pi->p_pid) {
			continue;
		}
		pi->p_state |= STATE_DEAD;
		printf("collected exit status from proc %d\n", pi->p_pid);
		printf("dumping TTY buffer:\n");
		termbuf_print_queue(&pi->p_ttybuf.t_head);
		prison_remove(pi);
	}
	pthread_mutex_unlock(&prison_mutex);
	reap_children = 0;
}

static int
tty_initialize_fdset(fd_set *rfds)
{
	struct prison_instance *pi, *p_temp;
	int maxfd;

	FD_ZERO(rfds);
	maxfd = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH_SAFE(pi, &pr_head, p_glue, p_temp) {
		if ((pi->p_state & STATE_DEAD) != 0) {
			continue;
		}
		if (pi->p_ttyfd > maxfd) {
			maxfd = pi->p_ttyfd;
		}
		FD_SET(pi->p_ttyfd, rfds);
	}
	pthread_mutex_unlock(&prison_mutex);
	return (maxfd);
}

void *
tty_io_queue_loop(void *arg)
{
	struct prison_instance *pi;
	struct timeval tv;
	u_char buf[8192];
	int maxfd, error;
	uint32_t cmd;
	fd_set rfds;
	ssize_t cc;
	size_t len;

	while (1) {
		prison_reap_children();
		maxfd = tty_initialize_fdset(&rfds);
		tv.tv_sec = 0;
		tv.tv_usec = 500000;
		error = select(maxfd + 1, &rfds, NULL, NULL, &tv);
		if (error == -1 && errno == EINTR) {
			printf("select interrupted\n");
			continue;
		}
		if (error == -1) {
			err(1, "select(tty io) failed");
		}
		if (error == 0) {
			continue;
		}
		pthread_mutex_lock(&prison_mutex);
		TAILQ_FOREACH(pi, &pr_head, p_glue) {
			if (!FD_ISSET(pi->p_ttyfd, &rfds)) {
				continue;
			}
			cc = read(pi->p_ttyfd, buf, sizeof(buf));
			if (cc == 0) {
				reap_children = 1;
				pi->p_state |= STATE_DEAD;
				continue;
			}
			if (cc == -1) {
				err(1, "%s: read failed:", __func__);
			}
			termbuf_append(&pi->p_ttybuf, buf, cc);
			if (pi->p_state != STATE_CONNECTED) {
				continue;
			}
			len = cc;
			cmd = PRISON_IPC_CONSOLE_TO_CLIENT;
			sock_ipc_must_write(pi->p_peer_sock, &cmd, sizeof(cmd));
			sock_ipc_must_write(pi->p_peer_sock, &len, sizeof(len));
			sock_ipc_must_write(pi->p_peer_sock, buf, cc);
		}
		pthread_mutex_unlock(&prison_mutex);
	}
}

static int
prison_instance_is_dead(const char *instance)
{
	struct prison_instance *pi;
	int isdead;

	isdead = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (!prison_instance_match(pi->p_instance_tag, instance)) {
			continue;
		}
		isdead = ((pi->p_state & STATE_DEAD) != 0);
		pthread_mutex_unlock(&prison_mutex);
		return (isdead);
        }
        pthread_mutex_unlock(&prison_mutex);
	/*
	 * The console was non-interactive (i.e.: a build job) and it is
	 * complete and the process(s) have been reaped. We might want
	 * to assert that this is the case some place.
	 */
        return (1);
}

void
dispatch_handle_resize(int ttyfd, char *buf)
{
	struct winsize *wsize;
	char *vptr;

	vptr = buf;
	vptr += sizeof(uint32_t);
	wsize = (struct winsize *)vptr;
	if (ioctl(ttyfd, TIOCSWINSZ, wsize) == -1) {
		err(1, "ioctl(TIOCSWINSZ): failed");
	}
}

void
tty_console_session(const char *instance, int sock, int ttyfd)
{
	char buf[1024], *vptr;
	uint32_t *cmd;
	ssize_t bytes;

	printf("tty_console_session: enter, reading commands from client\n");
	for (;;) {
		bzero(buf, sizeof(buf));
		ssize_t cc = read(sock, buf, sizeof(buf));
		if (cc == 0) {
			printf("read EOF from socket\n");
			break;
		}
		if (cc == -1 && errno == EINTR) {
			continue;
		}
		if (cc == -1) {
			err(1, "%s: read failed", __func__);
		}
		/*
		 * NB: There probably needs to be a better way to do this 
		 * rather than iterating through the jail list for read input
		 * from the console.
		 */
		if (prison_instance_is_dead(instance)) {
			break;
		}
		vptr = buf;
		cmd = (uint32_t *)vptr;
		switch (*cmd) {
		case PRISON_IPC_CONSOL_RESIZE:
			dispatch_handle_resize(ttyfd, buf);
			break;
		case PRISON_IPC_CONSOLE_DATA:
			vptr += sizeof(uint32_t);
			cc -= sizeof(uint32_t);
			bytes = write(ttyfd, vptr, cc);
			if (bytes != cc) {
				err(1, "tty_write failed");
			}
			break;
		default:
			errx(1, "unknown console instruction");
		}
	}
	printf("console dis-connected\n");
}

struct prison_instance *
prison_lookup_instance(const char *instance)
{
	struct prison_instance *pi;

	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (!prison_instance_match(pi->p_instance_tag, instance)) {
			continue;
		}
		return (pi);
	}
	return (NULL);
}

static char *
trim_tty_buffer(char *input, size_t len, size_t *newlen)
{
	uintptr_t old, new;
	char  *ep;

	new = 0;
	ep = input + len - 1;
	old = (uintptr_t) ep;
	while (ep > input) {
		if (isspace(*ep) || *ep == '\0') {
			ep--;
			continue;
		}
		new = (uintptr_t) ep;
		ep++;
		*ep = '\0';
		break;
	}
	*newlen = len - (old - new);
	return (input);
}

int
dispatch_connect_console(int sock)
{
	struct prison_console_connect pcc;
	struct prison_response resp;
	struct prison_instance *pi;
	ssize_t tty_buflen;
	char *tty_block;
	uint32_t cmd;
	size_t len;
	int ttyfd;

	bzero(&resp, sizeof(resp));
	sock_ipc_must_read(sock, &pcc, sizeof(pcc));
	pthread_mutex_lock(&prison_mutex);
	pi = prison_lookup_instance(pcc.p_instance);
	if (pi == NULL) {
		pthread_mutex_unlock(&prison_mutex);
		sprintf(resp.p_errbuf, "%s invalid container", pcc.p_instance);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	if ((pi->p_state & STATE_CONNECTED) != 0) {
		pthread_mutex_unlock(&prison_mutex);
		sprintf(resp.p_errbuf, "%s console already attached", pcc.p_instance);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	pi->p_state = STATE_CONNECTED;
	ttyfd = pi->p_ttyfd;
	tty_block = termbuf_to_contig(&pi->p_ttybuf);
	tty_buflen = pi->p_ttybuf.t_tot_len;
	pi->p_peer_sock = sock;
	pthread_mutex_unlock(&prison_mutex);
	resp.p_ecode = 0;
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	if (tty_block) {
		char *trimmed;

		cmd = PRISON_IPC_CONSOLE_TO_CLIENT;
		sock_ipc_must_write(sock, &cmd, sizeof(cmd));
		trimmed = trim_tty_buffer(tty_block, tty_buflen, &len);
		sock_ipc_must_write(sock, &len, sizeof(len));
		sock_ipc_must_write(sock, trimmed, len);
		free(tty_block);
	}
	if (tcsetattr(ttyfd, TCSANOW, &pcc.p_termios) == -1) {
		err(1, "tcsetattr(TCSANOW) console connect");
	}
	if (ioctl(ttyfd, TIOCSWINSZ, &pcc.p_winsize) == -1) {
		err(1, "ioctl(TIOCSWINSZ): failed");
	}
	/*
	 * If this console connection is the result of a container build, the
	 * build process will be blocked waiting for the console connection.
	 * write a single byte to the pipe to trigger the execution.
	 *
	 * NB: instead of checking the file descriptor, we should be using a
	 * flag
	 */
	if (pi->p_pipe[1] != 0) {
		char b;
		write(pi->p_pipe[1], &b, 1);
	}
	tty_console_session(pcc.p_instance, sock, ttyfd);
	prison_detach_console(pcc.p_instance);
	return (1);
}

struct prison_instance *
prison_create(const char *name, char *term, int (*prison_callback)(void *, struct prison_instance *),
    void *arg)
{
	struct prison_instance *pi;
	ssize_t cc;
	int ret;
	char b;

	pi = calloc(1, sizeof(*pi));
	if (pi == NULL) {
		return (NULL);
	}
	pi->p_type = PRISON_TYPE_BUILD;
	if (pipe(pi->p_pipe) == -1) {
		warn("pipe failed");
		return (NULL);
	}
	pi->p_pid = forkpty(&pi->p_ttyfd, pi->p_ttyname, NULL, NULL);
	if (pi->p_pid == 0) {
		close(pi->p_pipe[1]);
		while (1) {
			cc = read(pi->p_pipe[0], &b, 1);
			if (cc == -1 && errno == EINTR)
				continue;
			if (cc == -1)
				err(1, "%s: read failed", __func__);
			break;
			assert(cc == 1);
		}
		if (setenv("TERM", term, 1) != 0) {
			err(1, "setenv failed");
		}
		ret = (prison_callback)(arg, pi);
		_exit(ret);
	}
	prison_create_pid_file(pi);
	close(pi->p_pipe[0]);
	TAILQ_INIT(&pi->p_ttybuf.t_head);
	pi->p_ttybuf.t_tot_len = 0;
	return (pi);
}

int
dispatch_build_launch(int sock)
{
	struct prison_build_context pbc;
	struct prison_response resp;
	struct prison_instance *pi;
	struct build_context *bcp;
	char prison_name[32];
	ssize_t cc;

	cc = sock_ipc_must_read(sock, &pbc, sizeof(pbc));
	bcp = build_lookup_queued_context(&pbc);
	if (bcp == NULL) {
		resp.p_ecode = ENOENT;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (-1);
	}
	snprintf(prison_name, sizeof(prison_name), "%s:%s",
	    pbc.p_image_name, pbc.p_tag);
	pi = prison_create(prison_name, pbc.p_term, do_build_launch, bcp);
	if (pi == NULL) {
		resp.p_ecode = -1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (-1);
	}
	pi->p_instance_tag = bcp->instance;
	strlcpy(pi->p_image_name, pbc.p_image_name, sizeof(pi->p_image_name));
	pi->p_launch_time = time(NULL);
	pthread_mutex_lock(&prison_mutex);
	TAILQ_INSERT_HEAD(&pr_head, pi, p_glue);
	pthread_mutex_unlock(&prison_mutex);
	resp.p_ecode = 0;
	resp.p_errbuf[0] = '\0';
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	return (1);
}

void
gen_sha256_string(unsigned char *hash, char *output)
{
	int k;

	for (k = 0; k < SHA256_DIGEST_LENGTH; k++) {
		sprintf(output + (k * 2), "%02x", hash[k]);
	}
	output[64] = '\0';
}

char *
gen_sha256_instance_id(char *instance_name)
{
	u_char hash[SHA256_DIGEST_LENGTH];
	char inbuf[128], *ret;
	char outbuf[128+1];
	char buf[32];
	SHA256_CTX sha256;

	bzero(inbuf, sizeof(inbuf));
	arc4random_buf(inbuf, sizeof(inbuf) - 1);
	bzero(outbuf, sizeof(outbuf));
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, inbuf, strlen(inbuf));
	SHA256_Final(hash, &sha256);
	gen_sha256_string(&hash[0], outbuf);
	sprintf(buf, "%.10s", outbuf);
	ret = strdup(buf);
	return (ret);
}

int
dispatch_launch_prison(int sock)
{
	extern struct global_params gcfg;
	struct prison_response resp;
	struct prison_instance *pi;
	char **env, **argv, buf[128];
	struct prison_launch pl;
	ssize_t cc;
	vec_t *cmd_vec, *env_vec;

	cc = sock_ipc_must_read(sock, &pl, sizeof(pl));
	if (cc == 0) {
		return (0);
	}
	pi = calloc(1, sizeof(*pi));
	if (pi == NULL) {
		err(1, "calloc failed");
	}
	if (pl.p_entry_point_args[0] != '\0') {
		printf("Passing in command line arguments: %s\n", 
		    pl.p_entry_point_args);
	}
	pi->p_type = PRISON_TYPE_REGULAR;
	strlcpy(pi->p_image_name, pl.p_name, sizeof(pi->p_image_name));
	cmd_vec = vec_init(64);
	env_vec = vec_init(64);
	/*
	 * Setup the environment variables first.
	 */
	sprintf(buf, "TERM=%s", pl.p_term);
	vec_append(env_vec, buf);
	vec_finalize(env_vec);
	sprintf(buf, "%s/lib/stage_launch.sh", gcfg.c_data_dir);
	vec_append(cmd_vec, "/bin/sh");
	vec_append(cmd_vec, buf);
	vec_append(cmd_vec, gcfg.c_data_dir);
	vec_append(cmd_vec, pl.p_name);
	pi->p_instance_tag = gen_sha256_instance_id(pl.p_name);
	pi->p_launch_time = time(NULL);
	vec_append(cmd_vec, pi->p_instance_tag);
	vec_append(cmd_vec, pl.p_volumes);
	if (pl.p_network[0] != '\0') {
		vec_append(cmd_vec, pl.p_network);
	} else {
		vec_append(cmd_vec, "default");
	}
	if (pl.p_entry_point_args[0] != '\0') {
		vec_append(cmd_vec, pl.p_entry_point_args);
	}
	vec_finalize(cmd_vec);
	pi->p_pid = forkpty(&pi->p_ttyfd, pi->p_ttyname, NULL, NULL);
	if (pi->p_pid == 0) {
		argv = vec_return(cmd_vec);
		env = vec_return(env_vec);
		execve(*argv, argv, env);
		err(1, "execve failed");
	}
	prison_create_pid_file(pi);
	TAILQ_INIT(&pi->p_ttybuf.t_head);
	pi->p_ttybuf.t_tot_len = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_INSERT_HEAD(&pr_head, pi, p_glue);
	pthread_mutex_unlock(&prison_mutex);
	resp.p_ecode = 0;
	snprintf(resp.p_errbuf, sizeof(resp.p_errbuf), "%s",
	    pi->p_instance_tag);
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	return (1);
}

void *
dispatch_work(void *arg)
{
	struct prison_peer *p;
	uint32_t cmd;
	ssize_t cc;
	int done;

	signal(SIGCHLD, handle_reap_children);
	p = (struct prison_peer *)arg;
	printf("newly accepted socket: %d\n", p->p_sock);
	done = 0;
	while (!done) {
		cc = sock_ipc_may_read(p->p_sock, &cmd, sizeof(cmd));
		if (cc == 1) {
			break;
		}
		switch (cmd) {
		case PRISON_IPC_GENERIC_COMMAND:
			cc = dispatch_generic_command(p->p_sock);
			done = 1;
			break;
		case PRISON_IPC_GET_INSTANCES:
			cc = dispatch_get_instances(p->p_sock);
			break;
		case PRISON_IPC_LAUNCH_BUILD:
			cc = dispatch_build_launch(p->p_sock);
			break;
		case PRISON_IPC_SEND_BUILD_CTX:
			cc = dispatch_build_recieve(p->p_sock);
			done = 1;
			break;
		case PRISON_IPC_CONSOLE_CONNECT:
			cc = dispatch_connect_console(p->p_sock);
			done = 1;
			break;
		case PRISON_IPC_LAUNCH_PRISON:
			cc = dispatch_launch_prison(p->p_sock);
			done = 1;
			break;
		default:
			/*
			 * NB: maybe best to send a response
			 */
			warnx("unknown command %u", cmd);
			done = 1;
			break;
		}
	}
	close(p->p_sock);
	pthread_mutex_lock(&peer_mutex);
	TAILQ_REMOVE(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
	free(p);
	return (NULL);
}

void *
prison_handle_request(void *arg)
{
	struct prison_peer *p;

	p = (struct prison_peer *)arg;
	if (pthread_create(&p->p_thr, NULL, dispatch_work, arg) != 0) {
		err(1, "pthread_create(dispatch_work) failed");
	}
	pthread_mutex_lock(&peer_mutex);
	TAILQ_INSERT_HEAD(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
	return (NULL);
}
