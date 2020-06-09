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
#include <sys/wait.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/param.h>
#include <sys/un.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include <netdb.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <err.h>

pid_t
waitpid_ignore_intr(pid_t pid, int *status)
{
	pid_t rpid;

	while (1) {
		rpid = waitpid(pid, status, 0);
		if (rpid == -1 && errno == EINTR) {
			continue;
		} else if (pid == -1) {
			err(1, "waitpid failed");
		}
		break;
	}
	return (rpid);
}

void
sock_ipc_from_sock_to_tty(int sock)
{
	char buf[2048];
	ssize_t cc;

	while (1) {
		cc = read(sock, buf, sizeof(buf));
		if (cc == -1 && errno == EINTR) {
			continue;
		}
		if (cc == -1) {
			err(1, "read failed");
		}
		if (cc == 0) {
			break;
		}
		write(STDOUT_FILENO, buf, cc);
	}
}

int
sock_ipc_may_read(int fd, void *buf, size_t n)
{
	ssize_t res, pos;
	char *s;

	s = buf;
	pos = 0;
	while (n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			err(1, "%s: read failed", __func__);
		case 0:
			return (1);
		default:
			pos += res;
		}
	}
	return (0);
}

/*
 * Read data with the assertion that it all must come through, or
 * else abort the process.  Based on atomicio() from openssh. 
 */
ssize_t
sock_ipc_must_read(int fd, void *buf, size_t n)
{
	ssize_t res, pos;
	char *s;

	s = buf;
	pos = 0;
	while (n > pos) {
		res = read(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			err(1, "%s: read failed", __func__);
		case 0:
			return (0);
		default:
			pos += res;
		}
	}
	return (n);
}

/*
 * Write data with the assertion that it all has to be written, or
 * else abort the process.  Based on atomicio() from openssh.
 */
ssize_t
sock_ipc_must_write(int fd, void *buf, size_t n)
{
	ssize_t res, pos;
	char *s;

	pos = 0;
	s = buf;
	while (n > pos) {
		res = write(fd, s + pos, n - pos);
		switch (res) {
		case -1:
			if (errno == EINTR || errno == EAGAIN) {
				continue;
			}
			err(1, "sock_ipc_must_write failed");
		case 0:
			return (0);
		default:
			pos += res;
		}
	}
	return (n);
}

ssize_t
sock_ipc_from_to(int from, int to, off_t len)
{
	off_t block_count;
	size_t toread;
	int pagesize;
	u_char *buf;

	pagesize = getpagesize();
	buf = calloc(1, pagesize);
	if (buf == NULL) {
		err(1, "calloc failed");
	}
	for (block_count = 0; block_count < len; block_count += pagesize) {
		toread = pagesize;
		if ((pagesize + block_count) > len) {
			toread = len - block_count;
		}
		if (sock_ipc_must_read(from, buf, toread) != toread) {
			return (-1);
		}
		if (sock_ipc_must_write(to, buf, toread) != toread) {
			return (-1);
		}
	}
	return (len);
}
