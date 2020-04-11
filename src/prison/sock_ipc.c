#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
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
sock_ipc_connect_inet(struct global_params *gc)
{
	struct addrinfo hints, *res, *res0;
	int s, error;

	bzero(&hints, sizeof(hints));
	hints.ai_family = gc->c_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(gc->c_host, gc->c_port, &hints, &res0);
	if (error) {
		errx(1, "getaddrinfo failed: %s\n", gai_strerror(error));
	}
	res = res0;
	s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	if (s == -1) {
		err(1, "socket failed");
	}
	if (connect(s, res->ai_addr, res->ai_addrlen) == -1) {
		err(1, "connect failed");
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
		err(1, "bind(PF_UNIX) failed");
	}
	return (sock);
}
