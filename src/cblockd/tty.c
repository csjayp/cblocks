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
#include <stdint.h>

#include <openssl/sha.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"
#include "cblock.h"
#include "config.h"

#include <cblock/libcblock.h>

#define TERM_BUF_SIZE 4096

void
tty_set_raw(int fd) {
	struct termios t;

	if (tcgetattr(fd, &t) < 0) {
		err(1, "tcgetattr failed");
	}
	/*
	 * Unset the following on the terminal. It's possible that we are
	 * missing some parameters here, but we can re-visit this in the
	 * future if need be.
	 *
	 * ICANON | ECHO disable canonical mode and echo
	 * OPOST disable output processing (\n -> \r\n)
	 * IXON | ICRNL disable Ctrl-S/Q and CR->NL
	 */
	t.c_lflag &= ~(ICANON | ECHO);
	t.c_oflag &= ~(OPOST);
	t.c_iflag &= ~(IXON | ICRNL);
	if (tcsetattr(fd, TCSANOW, &t) < 0) {
		err(1, "tcsetattr failed");
	}
}

uint32_t
tty_read_header(int sock, int *goteof)
{
	unsigned char *header_ptr;
	size_t bytes_read;
	uint32_t header;
	ssize_t r;

	bytes_read = 0;
	header_ptr = (unsigned char *)&header;
	while (bytes_read < sizeof(header)) {
		r = read(sock, header_ptr + bytes_read, sizeof(header) - bytes_read);
		if (r == 0) {
			*goteof = 1;
			return (0);
		} else if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			err(1, "%s: read header failed", __func__);
		}
		bytes_read += r;
	}
	return (header);
}

void
tty_handle_resize(int ttyfd, struct winsize *wsize)
{
	/* Do the window re-size now */
	if (ioctl(ttyfd, TIOCSWINSZ, wsize) == -1) {
		err(1, "ioctl(TIOCSWINSZ): failed");
	}
}

struct winsize
tty_read_winsize_change(int sock, int *goteof)
{
	size_t payload_size, payload_read;
	unsigned char *payload_ptr;
	struct winsize wsize;
	ssize_t r;

	payload_ptr = (unsigned char *)&wsize;
	payload_size = sizeof(wsize);
	payload_read = 0;
	while (payload_read < payload_size) {
		r = read(sock, payload_ptr + payload_read, payload_size - payload_read);
		if (r == 0) {
			*goteof = 1;
			break;
		} else if (r < 0) {
			if (errno == EINTR) {
				continue;
			}
			err(1, "%s: read resize payload failed", __func__);
		}
		payload_read += r;
	}
	return (wsize);
}

void
tty_copy_from_sock_to_tty(int sock, int ttyfd, int *goteof)
{
	unsigned char buf[TERM_BUF_SIZE];
	ssize_t w, r, written;

	while ((r = read(sock, buf, sizeof(buf))) > 0) {
		if (r == 0) {
			*goteof = 1;
		}
		written = 0;
		while (written < r) {
			w = write(ttyfd, buf + written, r - written);
			if (w < 0) {
				err(1, "%s: tty write failed", __func__);
			}
			written += w;
		}
		if (r < TERM_BUF_SIZE) {
			break;
		}
		if (r < 0 && errno != EINTR) {
			err(1, "%s: tty read failed", __func__);
		}
	}
}

void
tty_console_session(const char *instance, int sock, int ttyfd) {
	struct winsize wsize;
	uint32_t header;
	int eof;

	printf("tty_console_session: enter, reading commands from client\n");
	tty_set_raw(ttyfd);
	eof = 0;
	while (!eof) {
		header = tty_read_header(sock, &eof);
		if (eof) {
			continue;
		}
		/*
		 * NB: There probably needs to be a better way to do this 
		 * rather than iterating through the jail list for read input
		 * from the console.
		 */
		if (cblock_instance_is_dead(instance)) {
			break;
		}
		switch (header) {
		case PRISON_IPC_CONSOL_RESIZE:
			wsize = tty_read_winsize_change(sock, &eof);
			if (eof) {
				continue;
			}
			tty_handle_resize(ttyfd, &wsize);
			break;
		case PRISON_IPC_CONSOLE_DATA:
			tty_copy_from_sock_to_tty(sock, ttyfd, &eof);
			if (eof) {
				continue;
			}
			break;
		default:
			errx(1, "unknown console instruction %u", header);
		}
	}
	printf("console disconnected\n");
}

char *
tty_trim_buffer(char *input, size_t len, size_t *newlen)
{
	uintptr_t old, new;
	char  *ep;

	if (len == 0) {
		*newlen = 0;
		return (input);
	}
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
