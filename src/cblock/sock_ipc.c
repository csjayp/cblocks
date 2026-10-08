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
#include <sys/param.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <err.h>

#include "main.h"

int
sock_ipc_connect_inet(struct global_params *gc)
{
	struct addrinfo hints, *res, *res0;
	int s, error, save_errno;

	bzero(&hints, sizeof(hints));
	hints.ai_family = gc->c_family;
	hints.ai_socktype = SOCK_STREAM;
	error = getaddrinfo(gc->c_host, gc->c_port, &hints, &res0);
	if (error) {
		errx(1, "%s: %s", gc->c_host, gai_strerror(error));
	}
	/*
	 * A host name can have several addresses, e.g. both AAAA and A
	 * records. Try each in turn and use the first one that connects.
	 */
	s = -1;
	save_errno = 0;
	for (res = res0; res != NULL; res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1) {
			save_errno = errno;
			continue;
		}
		if (connect(s, res->ai_addr, res->ai_addrlen) == 0) {
			break;
		}
		save_errno = errno;
		close(s);
		s = -1;
	}
	freeaddrinfo(res0);
	if (s == -1) {
		errno = save_errno;
		err(1, "connect to %s port %s failed", gc->c_host, gc->c_port);
	}
	return (s);
}

int
sock_ipc_connect_unix(struct global_params *gc)
{
	struct sockaddr_un addr;
	int sock;

	sock = socket(PF_UNIX, SOCK_STREAM, PF_UNSPEC);
	if (sock == -1) {
		err(1, "socket(PF_UNIX) failed");
	}
	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, gc->c_name, sizeof(addr.sun_path)-1);
	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		err(1, "connect(PF_UNIX) failed");
	}
	return (sock);
}

/*
 * Reach cblockd through ssh: run "ssh DEST nc -U PATH" and use one end of
 * a socket pair as the connection. The host is
 * ssh://[user@]host[:port][/socket/path]. ssh understands that URL form
 * itself, so only the socket path is split off here. Authentication, host
 * keys and ~/.ssh/config are all left to ssh.
 */
int
sock_ipc_connect_ssh(struct global_params *gc)
{
	char *dest, *path;
	int sv[2];
	pid_t pid;

	path = strchr(gc->c_host + strlen("ssh://"), '/');
	if (path == NULL) {
		dest = strdup(gc->c_host);
		path = gc->c_name;
	} else {
		dest = strndup(gc->c_host, path - gc->c_host);
	}
	if (dest == NULL) {
		err(1, "strdup failed");
	}
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
		err(1, "socketpair failed");
	}
	pid = fork();
	if (pid == -1) {
		err(1, "fork failed");
	}
	if (pid == 0) {
		close(sv[0]);
		if (dup2(sv[1], STDIN_FILENO) == -1 ||
		    dup2(sv[1], STDOUT_FILENO) == -1) {
			err(1, "dup2 failed");
		}
		close(sv[1]);
		/* cblock ignores SIGPIPE; ssh should not inherit that. */
		signal(SIGPIPE, SIG_DFL);
		execlp("ssh", "ssh", "-T", dest, "nc", "-U", path,
		    (char *)NULL);
		err(1, "failed to exec ssh");
	}
	close(sv[1]);
	free(dest);
	return (sv[0]);
}
