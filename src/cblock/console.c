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
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <sys/ttycom.h>

#include <netinet/in.h>

#include <stdio.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <err.h>
#include <stdint.h>
#include <locale.h>
#include <fcntl.h>
#include <unistd.h>

#include <cblock/libcblock.h>

#include "main.h"
#include "sock_ipc.h"

struct termios otermios;
int need_resize;

void	console_reset_tty(void);
int	console_mplex(int);

struct console_config {
	char		*c_name;
};

static struct option console_options[] = {
	{ "help",		no_argument, 0, 'h' },
	{ "name",		required_argument, 0, 'n' },
	{ 0, 0, 0, 0 }
};

static void
console_handle_window_resize(int sig __attribute__((unused)))
{

	need_resize = 1;
}

static void
console_usage(void)
{
	(void) fprintf(stderr,
	    "Usage: cblock console [OPTIONS]\n\n"
	    "Options\n"
	    " -h, --help        Display program usage\n"
	    " -n, --name        Instance ID for connection\n"
	);
	exit(1);
}

void
console_reset_tty(void)
{
	struct termios t;

	tcgetattr(STDIN_FILENO, &t);
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_iflag &= ~(IXON | ICRNL);
	t.c_oflag &= ~(OPOST);
	tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

int
console_tty_set_raw_mode(int fd)
{
	struct termios tbuf;

	/*
	 * We are committed to setting the TTY into raw raw mode, whatever
	 * happens make sure we restore the TTY state to a clean, and sane
	 * place to work.
	 */
	atexit(console_reset_tty);
	if (tcgetattr(fd, &otermios) == -1) {
		return (-1);
	}
	tbuf = otermios;
	tbuf.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	tbuf.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	tbuf.c_cflag &= ~(CSIZE | PARENB);
	tbuf.c_cflag |= CS8;
	tbuf.c_oflag &= ~(OPOST);
	tbuf.c_cc[VMIN] = 1;
	tbuf.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSAFLUSH, &tbuf) == -1) {
		return (-1);
	}
	return (0);
}

static int
console_tty_handle_socket(int sock)
{
	uint32_t cmd;
	size_t len;
	char *buf;

	if (sock_ipc_may_read(sock, &cmd, sizeof(cmd))) {
		return (1);
	}
	switch (cmd) {
	case PRISON_IPC_CONSOLE_TO_CLIENT:
		sock_ipc_must_read(sock, &len, sizeof(len));
		buf = malloc(len);
		sock_ipc_must_read(sock, buf, len);
		(void) write(STDIN_FILENO, buf, len);
		break;
	case PRISON_IPC_CONSOLE_SESSION_DONE:
		console_reset_tty();
		return (1);
		break;
	default:
		printf("invalid console frame type %d\n", cmd);
	}
	return (0);
}

static void console_tty_send_resize(int sock)
{
	unsigned char buf[sizeof(uint32_t) + sizeof(struct winsize)];
	struct winsize wsize;
	unsigned char *vptr;
	uint32_t cmd_val;

	vptr = buf;
	cmd_val = PRISON_IPC_CONSOL_RESIZE;
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &wsize) == -1) {
		err(1, "ioctl(TIOCGWINSZ) failed");
	}
	memcpy(vptr, &cmd_val, sizeof(cmd_val));
	vptr += sizeof(cmd_val);
	memcpy(vptr, &wsize, sizeof(wsize));
	ssize_t len = sizeof(buf);
	if (write(sock, buf, len) != len) {
		err(1, "tty send resize failed");
	}
}

static int
console_tty_handle_stdin(int sock)
{
	unsigned char buf[4096];
	struct iovec iov[2];
	ssize_t n, i, total;
	uint32_t header;

	n = read(STDIN_FILENO, buf, sizeof(buf));
	if (n == -1 && errno == EINTR) {
		return (0);
	} else if (n == -1) {
		err(1, "read failed");
	}
	for (i = 0; i < n; i++) {
		/* Ctrl+Q should be configurable */
		if (buf[i] == 0x11) {
			(void) fprintf(stderr,
			    "\n\n[Ctrl-Q: disconnect sequence]\n");
			close(sock);
			return (1);
		}
	}
	if (need_resize) {
		console_tty_send_resize(sock);
		need_resize = 0;
	}
	header = PRISON_IPC_CONSOLE_DATA;
	iov[0].iov_base = &header;
	iov[0].iov_len  = sizeof(header);
	iov[1].iov_base = buf;
	iov[1].iov_len  = n;
	total = writev(sock, iov, 2);
	if (total != (ssize_t)(sizeof(header) + n)) {
		perror("writev header+data");
		close(sock);
		return (1);
	}
	return (0);
}

static void
console_evloop(int sock)
{
	int done;

	console_tty_set_raw_mode(STDIN_FILENO);
	done = 0;
	while (!done) {
		done = console_mplex(sock);
	}
}

int
console_mplex(int sock)
{
	fd_set rfds;
	int error;

	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	FD_SET(STDIN_FILENO, &rfds);
	error = select(sock + 1, &rfds, NULL, NULL, NULL);
	if (error == -1 && errno == EINTR) {
		return (0);
	}
	if (error == -1) {
		err(1, "select failed");
	}
	if (FD_ISSET(sock, &rfds)) {
		if (console_tty_handle_socket(sock)) {
			return (1);
		}
	}
	if (FD_ISSET(STDIN_FILENO, &rfds)) {
		if (console_tty_handle_stdin(sock)) {
			return (1);
		}
        }
	return (0);
}

void
console_tty_console_session(int sock)
{

	signal(SIGWINCH, console_handle_window_resize);
	console_evloop(sock);
}

static void
console_connect_console(int sock, struct console_config *ccp)
{
	struct cblock_console_connect pcc;
	struct cblock_response resp;
	uint32_t cmd;

	bzero(&pcc, sizeof(pcc));
	cmd = PRISON_IPC_CONSOLE_CONNECT;
	if (tcgetattr(STDIN_FILENO, &pcc.p_termios) == -1) {
		err(1, "tcgetattr(STDIN_FILENO) failed");
	}
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &pcc.p_winsize) == -1) {
		err(1, "ioctl(TIOCGWINSZ): failed");
	}
	strlcpy(pcc.p_instance, ccp->c_name, sizeof(pcc.p_instance));
	strlcpy(pcc.p_name, ccp->c_name, sizeof(pcc.p_name));
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	sock_ipc_must_write(sock, &pcc, sizeof(pcc));
	sock_ipc_must_read(sock, &resp, sizeof(resp));
	if (resp.p_ecode != 0) {
		(void) printf("failed to attach console to %s: %s\n",
		    ccp->c_name, resp.p_errbuf);
		return;
	}
	console_tty_console_session(sock);
}

int
console_main(int argc, char *argv [], int cltlsock)
{
	struct console_config cc;
	int option_index, c;

	setlocale(LC_CTYPE, "C.UTF-8");
	bzero(&cc, sizeof(cc));
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "n:h", console_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'h':
			console_usage();
			exit(1);
		case 'n':
			cc.c_name = optarg;
			break;
		}
	}
	if (cc.c_name == NULL) {
		errx(1, "must specify intance id to connect to");
	}
	console_connect_console(cltlsock, &cc);
	return (0);
}
