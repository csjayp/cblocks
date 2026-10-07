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
#include <sys/un.h>

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
volatile sig_atomic_t need_resize;

/*
 * When stdin is not a terminal (e.g.: CI runners) we do not touch terminal
 * modes, and we stop reading stdin once it reaches EOF.
 */
static int console_is_tty;
static int console_stdin_open;

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

	if (!console_is_tty) {
		return;
	}
	tcsetattr(STDIN_FILENO, TCSANOW, &otermios);
}

int
console_tty_set_raw_mode(int fd)
{
	struct termios tbuf;

	if (tcgetattr(fd, &otermios) == -1) {
		return (-1);
	}
	/*
	 * We are committed to setting the TTY into raw raw mode, whatever
	 * happens make sure we restore the TTY state to a clean, and sane
	 * place to work.
	 */
	atexit(console_reset_tty);
	tbuf = otermios;
	tbuf.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	tbuf.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	tbuf.c_cflag &= ~(CSIZE | PARENB);
	tbuf.c_cflag |= CS8;
	tbuf.c_cc[VMIN] = 1;
	tbuf.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSAFLUSH, &tbuf) == -1) {
		return (-1);
	}
	return (0);
}

/*
 * Use the size of the controlling terminal if we have one, otherwise fall
 * back to the classic 80x24.
 */
static void
console_get_winsize(struct winsize *wsize)
{

	if (console_is_tty &&
	    ioctl(STDIN_FILENO, TIOCGWINSZ, wsize) != -1) {
		return;
	}
	bzero(wsize, sizeof(*wsize));
	wsize->ws_row = 24;
	wsize->ws_col = 80;
}

static int
console_tty_handle_socket(int sock)
{
	const void *buf;
	struct wire w;
	uint32_t cmd;
	size_t len;

	if (sock_ipc_read_u32(sock, &cmd) == -1) {
		return (1);
	}
	switch (cmd) {
	case PRISON_IPC_CONSOLE_TO_CLIENT:
		if (wire_recv(sock, &w) == -1) {
			warnx("console: failed to read console frame");
			return (1);
		}
		buf = w.w_buf;
		len = w.w_len;
		if (len > 0 && sock_ipc_must_write(STDOUT_FILENO,
		    (void *)(uintptr_t)buf, len) != (ssize_t)len) {
			wire_free(&w);
			err(1, "console: write to stdout failed");
		}
		wire_free(&w);
		break;
	case PRISON_IPC_CONSOLE_SESSION_DONE:
		console_reset_tty();
		return (1);
		break;
	default:
		warnx("invalid console frame type %u", cmd);
		return (1);
	}
	return (0);
}

static void
console_tty_send_resize(int sock)
{
	struct winsize wsize;

	console_get_winsize(&wsize);
	if (sock_ipc_write_u32(sock, PRISON_IPC_CONSOL_RESIZE) == -1 ||
	    proto_send_winsize(sock, &wsize) == -1) {
		err(1, "tty send resize failed");
	}
}

static int
console_tty_handle_stdin(int sock)
{
	unsigned char buf[4096];
	ssize_t n, i;

	n = read(STDIN_FILENO, buf, sizeof(buf));
	if (n == -1 && errno == EINTR) {
		return (0);
	} else if (n == -1) {
		err(1, "read failed");
	}
	if (n == 0) {
		/*
		 * Nothing more to send, but keep relaying output until the
		 * session is done.
		 */
		console_stdin_open = 0;
		return (0);
	}
	if (console_is_tty) {
		for (i = 0; i < n; i++) {
			/* Ctrl+Q should be configurable */
			if (buf[i] == 0x11) {
				(void) fprintf(stderr,
				    "\n\n[Ctrl-Q: disconnect sequence]\n");
				close(sock);
				return (1);
			}
		}
	}
	if (wire_send_frame(sock, PRISON_IPC_CONSOLE_DATA, buf, n) == -1) {
		warn("console: failed to send data");
		close(sock);
		return (1);
	}
	return (0);
}

static void
console_evloop(int sock)
{
	int done;

	if (console_is_tty) {
		console_tty_set_raw_mode(STDIN_FILENO);
	}
	done = 0;
	while (!done) {
		done = console_mplex(sock);
	}
}

int
console_mplex(int sock)
{
	fd_set rfds;
	int error, maxfd;

	if (need_resize) {
		need_resize = 0;
		console_tty_send_resize(sock);
	}
	FD_ZERO(&rfds);
	FD_SET(sock, &rfds);
	maxfd = sock;
	if (console_stdin_open) {
		FD_SET(STDIN_FILENO, &rfds);
		maxfd = MAX(sock, STDIN_FILENO);
	}
	error = select(maxfd + 1, &rfds, NULL, NULL, NULL);
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
	if (console_stdin_open && FD_ISSET(STDIN_FILENO, &rfds)) {
		if (console_tty_handle_stdin(sock)) {
			return (1);
		}
	}
	return (0);
}

void
console_tty_console_session(int sock)
{

	console_is_tty = isatty(STDIN_FILENO);
	console_stdin_open = 1;
	if (console_is_tty) {
		signal(SIGWINCH, console_handle_window_resize);
	}
	console_evloop(sock);
}

static void
console_connect_console(int sock, struct console_config *ccp)
{
	struct cblock_console_connect pcc;
	struct cblock_response resp;

	console_is_tty = isatty(STDIN_FILENO);
	bzero(&pcc, sizeof(pcc));
	console_get_winsize(&pcc.p_winsize);
	snprintf(pcc.p_instance, sizeof(pcc.p_instance), "%s", ccp->c_name);
	snprintf(pcc.p_name, sizeof(pcc.p_name), "%s", ccp->c_name);
	if (sock_ipc_write_u32(sock, PRISON_IPC_CONSOLE_CONNECT) == -1 ||
	    proto_send_console_connect(sock, &pcc) == -1) {
		errx(1, "failed to send console connect request");
	}
	if (proto_recv_response(sock, &resp) == -1) {
		errx(1, "failed to read console connect response");
	}
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
