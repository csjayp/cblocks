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

#include <libcblock.h>

static int
tty_initialize_fdset(fd_set *rfds)
{
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
		if (cblock_instance_is_dead(instance)) {
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

static char *
tty_trim_buffer(char *input, size_t len, size_t *newlen)
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
		sprintf(resp.p_errbuf, "%s invalid container", pcc.p_instance);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	if ((pi->p_state & STATE_CONNECTED) != 0) {
		pthread_mutex_unlock(&cblock_mutex);
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
	pthread_mutex_unlock(&cblock_mutex);
	resp.p_ecode = 0;
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	if (tty_block) {
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
	tty_console_session(pcc.p_instance, sock, ttyfd);
	cblock_detach_console(pcc.p_instance);
	return (1);
}
