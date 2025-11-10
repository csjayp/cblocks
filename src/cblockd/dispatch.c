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
#include "cblock.h"

#include "probes.h"

#include <cblock/libcblock.h>

static int reap_children;

static void
handle_reap_children(int sig)
{

	reap_children = 1;
}

static int
tty_initialize_fdset(fd_set *rfds)
{
	extern cblock_instance_head_t pr_head;
	extern pthread_mutex_t cblock_mutex;
	struct cblock_instance *pi, *p_temp;
	int maxfd;

	FD_ZERO(rfds);
	maxfd = 0;
	pthread_mutex_lock(&cblock_mutex);
	TAILQ_FOREACH_SAFE(pi, &pr_head, p_glue, p_temp) {
		if ((pi->p_state & STATE_DEAD) != 0) {
			continue;
		}
		if (pi->p_ttyfd > maxfd) {
			maxfd = pi->p_ttyfd;
		}
		FD_SET(pi->p_ttyfd, rfds);
	}
	pthread_mutex_unlock(&cblock_mutex);
	return (maxfd);
}

void *
tty_io_queue_loop(void *arg)
{
	extern cblock_instance_head_t pr_head;
	extern pthread_mutex_t cblock_mutex;
	struct cblock_instance *pi;
	struct timeval tv;
	u_char buf[8192];
	int maxfd, error;
	uint32_t cmd;
	fd_set rfds;
	ssize_t cc;
	size_t len;

	while (1) {
		cblock_reap_children();
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
		pthread_mutex_lock(&cblock_mutex);
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
		pthread_mutex_unlock(&cblock_mutex);
	}
}

int
dispatch_signal_instance(int sock)
{
	extern pthread_mutex_t cblock_mutex;
	struct cblock_signal_instance csi;
	struct cblock_response resp;
	struct cblock_instance *pi;

	bzero(&resp, sizeof(resp));
	sock_ipc_must_read(sock, &csi, sizeof(csi));
	pthread_mutex_lock(&cblock_mutex);
	pi = cblock_lookup_instance(csi.p_instance);
	if (pi == NULL) {
		pthread_mutex_unlock(&cblock_mutex);
		(void) snprintf(resp.p_errbuf, sizeof(resp.p_errbuf),
		    "%s invalid container", csi.p_instance);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	switch (csi.p_sig) {
	case SIGTERM:
	case SIGKILL:
	case SIGHUP:
		(void) kill(pi->p_pid, csi.p_sig);
		pthread_mutex_unlock(&cblock_mutex);
		break;
	default:
		pthread_mutex_unlock(&cblock_mutex);
		(void) snprintf(resp.p_errbuf, sizeof(resp.p_errbuf),
		    "illegal signal specification: %d", csi.p_sig);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	pthread_mutex_unlock(&cblock_mutex);
	resp.p_ecode = 0;
	snprintf(resp.p_errbuf, sizeof(resp.p_errbuf), "OK %d", csi.p_sig);
	sock_ipc_must_write(sock, &resp, sizeof(resp));
        return (1);
}

int
dispatch_connect_console(int sock)
{
	extern pthread_mutex_t cblock_mutex;
	struct cblock_console_connect pcc;
	struct cblock_response resp;
	struct cblock_instance *pi;
	char *tty_block, *trimmed;
	ssize_t tty_buflen;
	uint32_t cmd;
	size_t len;
	int ttyfd;

	bzero(&resp, sizeof(resp));
	sock_ipc_must_read(sock, &pcc, sizeof(pcc));
	pthread_mutex_lock(&cblock_mutex);
	pi = cblock_lookup_instance(pcc.p_instance);
	if (pi == NULL) {
		pthread_mutex_unlock(&cblock_mutex);
		snprintf(resp.p_errbuf, sizeof(resp.p_errbuf),
		    "%s invalid container", pcc.p_instance);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	if ((pi->p_state & STATE_CONNECTED) != 0) {
		pthread_mutex_unlock(&cblock_mutex);
		snprintf(resp.p_errbuf, sizeof(resp.p_errbuf),
		    "%s console already attached", pcc.p_instance);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	CBLOCKD_CBLOCK_CONSOLE_ATTACH(pcc.p_instance);
	pi->p_state = STATE_CONNECTED;
	ttyfd = pi->p_ttyfd;
	tty_block = termbuf_to_contig(&pi->p_ttybuf);
	tty_buflen = pi->p_ttybuf.t_tot_len;
	pi->p_peer_sock = sock;
	pthread_mutex_unlock(&cblock_mutex);
	resp.p_ecode = 0;
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	if (tty_block) {
		cmd = PRISON_IPC_CONSOLE_TO_CLIENT;
		sock_ipc_must_write(sock, &cmd, sizeof(cmd));
		trimmed = tty_trim_buffer(tty_block, tty_buflen, &len);
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
	tty_console_session(pcc.p_instance, sock, ttyfd);
	cblock_detach_console(pcc.p_instance);
	return (1);
}

int
dispatch_launch_cblock(int sock)
{
	extern cblock_instance_head_t pr_head;
	extern pthread_mutex_t cblock_mutex;
	extern struct global_params gcfg;
	char **env, **argv, buf[128];
	struct cblock_response resp;
	struct cblock_instance *pi;
	vec_t *cmd_vec, *env_vec;
	struct cblock_launch pl;
	ssize_t cc;

	cc = sock_ipc_must_read(sock, &pl, sizeof(pl));
	if (cc == 0) {
		return (0);
	}
	pi = calloc(1, sizeof(*pi));
	if (pi == NULL) {
		err(1, "calloc failed");
	}
	pi->p_type = PRISON_TYPE_REGULAR;
	strlcpy(pi->p_image_name, pl.p_name, sizeof(pi->p_image_name));
	cmd_vec = vec_init(64);
	env_vec = vec_init(64);
	/*
	 * Setup the environment variables first.
	 */
	if (pl.p_ports[0] == '\0') {
		strcpy(pl.p_ports, "none");
	}
	snprintf(buf, sizeof(buf), "TERM=%s", pl.p_term);
	vec_append(env_vec, buf);
	vec_append(env_vec, "USER=root");
	vec_append(env_vec, "HOME=/root");
	sprintf(buf, "CBLOCK_FS=%s", gcfg.c_underlying_fs);
	vec_append(env_vec, buf);
	vec_finalize(env_vec);
	sprintf(buf, "%s/lib/stage_launch.sh", gcfg.c_data_dir);
	vec_append(cmd_vec, "/bin/sh");
	if (pl.p_verbose > 0) {
		vec_append(cmd_vec, "-x");
	}
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
	vec_append(cmd_vec, pl.p_tag);
	vec_append(cmd_vec, pl.p_ports);
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
	cblock_create_pid_file(pi);
	TAILQ_INIT(&pi->p_ttybuf.t_head);
	pi->p_ttybuf.t_tot_len = 0;
	pthread_mutex_lock(&cblock_mutex);
	CBLOCKD_CBLOCK_CREATE(pi->p_instance_tag);
	TAILQ_INSERT_HEAD(&pr_head, pi, p_glue);
	pthread_mutex_unlock(&cblock_mutex);
	bzero(&resp, sizeof(resp));
	resp.p_ecode = 0;
	snprintf(resp.p_errbuf, sizeof(resp.p_errbuf), "%s",
	    pi->p_instance_tag);
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	return (1);
}

void *
dispatch_work(void *arg)
{
	extern pthread_mutex_t peer_mutex;
	extern cblock_peer_head_t p_head;
	struct cblock_peer *p;
	uint32_t cmd;
	ssize_t cc;
	int done;

	signal(SIGCHLD, handle_reap_children);
	p = (struct cblock_peer *)arg;
	printf("newly accepted socket: %d\n", p->p_sock);
	done = 0;
	while (!done) {
		printf("waiting for command\n");
		cc = sock_ipc_may_read(p->p_sock, &cmd, sizeof(cmd));
		if (cc == 1) {
			break;
		}
		switch (cmd) {
		case PRISON_IPC_SIGNAL_INSTANCE:
			cc = dispatch_signal_instance(p->p_sock);
			done = 1;
			break;
		case PRISON_IPC_GENERIC_COMMAND:
			cc = dispatch_generic_command(p->p_sock);
			done = 1;
			break;
		case PRISON_IPC_GET_INSTANCES:
			cc = dispatch_get_instances(p->p_sock);
			break;
		case PRISON_IPC_SEND_BUILD_CTX:
			cc = dispatch_build_recieve(p->p_sock);
			break;
		case PRISON_IPC_CONSOLE_CONNECT:
			cc = dispatch_connect_console(p->p_sock);
			done = 1;
			break;
		case PRISON_IPC_LAUNCH_PRISON:
			cc = dispatch_launch_cblock(p->p_sock);
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
