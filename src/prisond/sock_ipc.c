#include <sys/types.h>
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

#include "main.h"
#include "sock_ipc.h"

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

static struct prison_peer *
sock_ipc_construct_peer(int sock, int family)
{
	struct prison_peer *p;

	p = calloc(1, sizeof(*p));
	if (p == NULL) {
		err(1, "calloc(prison peer) failed");
	}
	p->p_sock = sock;
	p->p_family = family;
	return (p);
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

static int
sock_ipc_accept_connection(int sock)
{
	struct sockaddr_storage addrs;
	struct prison_peer *p;
	struct sockaddr sa;
	socklen_t slen;
	int nsock;
	uid_t uid;
	gid_t gid;

	slen = sizeof(sa);
	if (getsockname(sock, &sa, &slen) == -1) {
		err(1, "getsockname failed");
	}
	switch (sa.sa_family) {
	case PF_UNIX:
		slen = sizeof(struct sockaddr_un);
		break;
	case PF_INET:
		slen = sizeof(struct sockaddr_in);
		break;
	case PF_INET6:
		slen = sizeof(struct sockaddr_in6);
		break;
	default:
		errx(1, "un-supported address family");
	}
	while (1) {
		nsock = accept(sock, (struct sockaddr *)&addrs, &slen);
		if (nsock == -1 && errno == EINTR) {
			continue;
		}
		if (nsock == -1) {
			err(1, "accept failed");
		}
		if (sa.sa_family == PF_UNIX) {
			if (getpeereid(nsock, &uid, &gid) == -1) {
				err(1, "getpeereid failed");
			}
			printf("user %d gid %d\n", uid, gid);
		}
		printf("accepted connection %d\n", nsock);
		p = sock_ipc_construct_peer(nsock, sa.sa_family);
		(void) (*gcfg.c_callback)(p);
	}
	return (0);
}

int
sock_ipc_event_loop(struct global_params *gcp)
{
	fd_set rfds;
	int error, maxfd, s;

	while (1) {
		maxfd = 0;
		FD_ZERO(&rfds);
		for (s = 0; s < gcp->c_sock_count; s++) {
			if (gcp->c_socks[s] > maxfd) {
				maxfd = gcp->c_socks[s];
			}
			FD_SET(gcp->c_socks[s], &rfds);
		}
		error = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (error == -1 && errno == EINTR) {
			continue;
		}
		if (error == -1) {
			err(1, "select failed");
		}
		printf("scanning fd set\n");
		for (s = 0; s < gcp->c_sock_count; s++) {
			if (!FD_ISSET(gcp->c_socks[s], &rfds)) {
				continue;
			}
			(void) sock_ipc_accept_connection(gcp->c_socks[s]);
		}
	}
	return (0);
}
