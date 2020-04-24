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

TAILQ_HEAD( , prison_peer) p_head;
TAILQ_HEAD( , prison_instance) pr_head;

static int reap_children;

pthread_mutex_t peer_mutex;
pthread_mutex_t prison_mutex;

static void
handle_reap_children(int sig)
{

	reap_children = 1;
}

void
prison_remove(struct prison_instance *pi)
{
	size_t cur;

	(void) close(pi->p_peer_sock);
	(void) close(pi->p_ttyfd);
	TAILQ_REMOVE(&pr_head, pi, p_glue);
	cur = pi->p_ttybuf.t_tot_len;
	while (cur > 0) {
		cur = termbuf_remove_oldest(&pi->p_ttybuf);
	}
	free(pi);
}

static void
prison_detach_console(const char *name)
{
	struct prison_instance *pi;

	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (strcmp(pi->p_name, name) != 0) {
			continue;
		}
		pi->p_state &= ~STATE_CONNECTED;
		pi->p_peer_sock = -1;
		pthread_mutex_unlock(&prison_mutex);
		return;
	}
	pthread_mutex_unlock(&prison_mutex);
	err(1, "attempt to detach non-exisent prison");
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
	int maxfd, error;
	u_char buf[8192];
	ssize_t cc, dd;
	fd_set rfds;

	printf("tty_io_queue_loop: dispatched\n");
	while (1) {
		prison_reap_children();
		maxfd = tty_initialize_fdset(&rfds);
		tv.tv_sec = 1;
		tv.tv_usec = 0;
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
				printf("state dead for %s\n", pi->p_name);
				reap_children = 1;
				pi->p_state |= STATE_DEAD;
				continue;
			}
			if (cc == -1) {
				err(1, "read failed:");
			}
			termbuf_append(&pi->p_ttybuf, buf, cc);
			printf("%s: queued %zu bytes for console: %zu\n",
			    pi->p_name, cc, pi->p_ttybuf.t_tot_len);
			if (pi->p_state != STATE_CONNECTED) {
				continue;
			}
			dd = write(pi->p_peer_sock, buf, cc);
			if (dd == -1 && errno == EPIPE) {
				err(1, "handled disappearing consoles here");
			}
			printf("%zu bytes written to console\n", dd);
		}
		pthread_mutex_unlock(&prison_mutex);
	}
}

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

static int
prison_instance_is_unique(char *name)
{
	struct prison_instance *pi;

	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (strcmp(pi->p_name, name) == 0) {
			pthread_mutex_unlock(&prison_mutex);
			return (0);
		}
	}
	pthread_mutex_unlock(&prison_mutex);
	return (1);
}

static int
prison_instance_is_dead(const char *name)
{
	struct prison_instance *pi;
	int isdead;

	isdead = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (strcmp(pi->p_name, name) != 0) {
			continue;
		}
		isdead = ((pi->p_state & STATE_DEAD) != 0);
		pthread_mutex_unlock(&prison_mutex);
		return (isdead);
        }
        pthread_mutex_unlock(&prison_mutex);
	err(1, "prison_instance_is_dead: on non-existent instance");
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
tty_console_session(const char *name, int sock, int ttyfd)
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
			err(1, "read failed");
		}
		/*
		 * NB: There probably needs to be a better way to do this 
		 * rather than iterating through the jail list for read input
		 * from the console.
		 */
		if (prison_instance_is_dead(name)) {
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
prison_lookup_instance(const char *name)
{
	struct prison_instance *pi;

	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (strcmp(name, pi->p_name) != 0) {
			continue;
		}
		return (pi);
	}
	return (NULL);
}

int
dispatch_connect_console(int sock)
{
	struct prison_console_connect pcc;
	struct prison_response resp;
	struct prison_instance *pi;
	ssize_t tty_buflen;
	char *tty_block;
	int ttyfd;

	bzero(&resp, sizeof(resp));
	sock_ipc_must_read(sock, &pcc, sizeof(pcc));
	printf("got console connect for container %s\n", pcc.p_name);
	pthread_mutex_lock(&prison_mutex);
	pi = prison_lookup_instance(pcc.p_name);
	if (pi == NULL) {
		pthread_mutex_unlock(&prison_mutex);
		sprintf(resp.p_errbuf, "%s invalid container", pcc.p_name);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	if ((pi->p_state & STATE_CONNECTED) != 0) {
		pthread_mutex_unlock(&prison_mutex);
		sprintf(resp.p_errbuf, "%s console already attached", pcc.p_name);
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
	printf("console connected for %s\n", pcc.p_name);
	resp.p_ecode = 0;
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	if (tty_block) {
		sock_ipc_must_write(sock, tty_block, tty_buflen);
		free(tty_block);
	}
	if (tcsetattr(ttyfd, TCSANOW, &pcc.p_termios) == -1) {
		err(1, "tcsetattr(TCSANOW) console connect");
	}
	if (ioctl(ttyfd, TIOCSWINSZ, &pcc.p_winsize) == -1) {
		err(1, "ioctl(TIOCSWINSZ): failed");
	}
	tty_console_session(pcc.p_name, sock, ttyfd);
	prison_detach_console(pcc.p_name);
	return (1);
}

int
dispatch_launch_prison(int sock)
{
	extern struct global_params gcfg;
	struct prison_response resp;
	struct prison_instance *pi;
	char *env[32], *argv[32];
	struct prison_launch pl;
	ssize_t cc;

	cc = sock_ipc_must_read(sock, &pl, sizeof(pl));
	if (cc == 0) {
		return (0);
	}
	if (!prison_instance_is_unique(pl.p_name)) {
		resp.p_ecode = 1;
		sprintf(resp.p_errbuf, "prison already exists");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	printf("launching prison %s\n", pl.p_name);
	pi = calloc(1, sizeof(*pi));
	if (pi == NULL) {
		err(1, "calloc failed");
	}
	strlcpy(pi->p_name, pl.p_name, sizeof(pi->p_name));
	printf("creating process with TERM=%s\n", pl.p_term);
	env[0] = strdup(pl.p_term);
	env[1] = NULL;
	pi->p_pid = forkpty(&pi->p_ttyfd, pi->p_ttyname, NULL, NULL);
	if (pi->p_pid == 0) {
		char buf[64];
		tty_set_noecho(STDIN_FILENO);
		sprintf(buf, "TERM=%s", pl.p_term);
		env[0] = strdup(buf);
		env[1] = NULL;
		argv[0] = "/bin/tcsh";
		argv[1] = NULL;
		execve(*argv, argv, env);
		err(1, "execve failed");
	}
	TAILQ_INIT(&pi->p_ttybuf.t_head);
	pi->p_ttybuf.t_tot_len = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_INSERT_HEAD(&pr_head, pi, p_glue);
	pthread_mutex_unlock(&prison_mutex);
	printf("launched shell as pid %d\n", pi->p_pid);
	resp.p_ecode = 0;
	resp.p_errbuf[0] = '\0';
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	return (1);
}

int
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

int
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

void *
dispatch_work(void *arg)
{
	struct prison_peer *p;
	uint32_t cmd;
	ssize_t cc;
	int done;

	signal(SIGCHLD, handle_reap_children);
	p = (struct prison_peer *)arg;
	done = 0;
	while (!done) {
		printf("waiting for command\n");
		cc = sock_ipc_may_read(p->p_sock, &cmd, sizeof(cmd));
		if (cc == 1) {
			break;
		}
		printf("read command %d\n", cmd);
		switch (cmd) {
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
			if (cc == 0) {
				done = 1;
				continue;
			}
			break;
		default:
			/*
			 * NB: maybe best to send a response
			 */
			warnx("unknown command %u", cmd);
			close(p->p_sock);
			break;
		}
	}
	printf("peer disconnected\n");
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

	printf("%s\n", __func__);
	p = (struct prison_peer *)arg;
	if (pthread_create(&p->p_thr, NULL, dispatch_work, arg) != 0) {
		err(1, "pthread_create(dispatch_work) failed");
	}
	pthread_mutex_lock(&peer_mutex);
	TAILQ_INSERT_HEAD(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
	return (NULL);
}
