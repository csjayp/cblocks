#include <sys/types.h>
#include <sys/socket.h>
#include <sys/param.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <netdb.h>
#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <err.h>

#include "main.h"

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
		case 0:
			return (0);
		default:
			pos += res;
		}
	}
	return (n);
}

int
sock_ipc_setup_unix(struct global_params *cmd)
{
	struct sockaddr_un addr;

	(void) unlink(cmd->c_name);
	cmd->c_socks[0] = socket(PF_UNIX, SOCK_STREAM, PF_UNSPEC);
	if (cmd->c_socks[0] == -1) {
		err(1, "socket(PF_UNIX) failed");
	}
	cmd->c_sock_count = 1;
	bzero(&addr, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, cmd->c_name, sizeof(addr.sun_path)-1);
	if (bind(cmd->c_socks[0], (struct sockaddr*)&addr, sizeof(addr)) == -1) {
		err(1, "bind(PF_UNIX) failed");
	}
	if (listen(cmd->c_socks[0], 100) == -1) {
		err(1, "listen(PF_UNIX) failed");
	}
	return (0);
}

int
sock_ipc_setup_inet(struct global_params *cmd)
{
	struct addrinfo hints, *res, *res0;
	int s, o, count, error;

	bzero(&hints, sizeof(hints));
	hints.ai_family = cmd->c_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(cmd->c_host, cmd->c_port, &hints, &res0);
	if (error) {
		errx(1, "getaddrinfo failed: %s\n", gai_strerror(error));
	}
	o = 1;
	count = 0;
	for (res = res0; res != NULL && count < MAXSOCKS;
	    res = res->ai_next) {
		s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (s == -1) {
			err(1, "socket");
		}
		if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &o, sizeof(o)) == -1) {
			err(1, "setsockopt(SO_REUSEADDR)");
		}
		if (res->ai_family == PF_INET6) {
			if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &o,
			    sizeof(o)) == -1) {
				err(1, "setsockopt(IPV6_V6ONLY)");
			}
		}
		if (bind(s, res->ai_addr, res->ai_addrlen) == -1) {
			err(1, "bind failed");
		}
		if (listen(s, 128) == -1) {
			err(1, "listen failed");
		}
		cmd->c_socks[cmd->c_sock_count++] = s;
	}
	freeaddrinfo(res0);
	return (0);
}
