/*-
 * Copyright (c) 2026 Christian S.J. Peron
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

#include <netdb.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "main.h"
#include "picohttpparser.h"

struct host_port {
	char		*host;
	char		*port;
};

static int max_socks = 128;

int
metrics_init_host_port(char *hostspec, struct host_port *hp)
{
	char *copy, *ptr;

	if (hp == NULL || hostspec == NULL) {
		return (-1);
	}
	copy = strdup(hostspec);
	if (copy == NULL) {
		return (-1);
	}
	ptr = index(copy, ':');
	if (ptr == NULL) {
		return (-1);
	}
	*ptr = '\0';
	hp->host = copy;
	ptr++;
	hp->port = ptr;
	return (0);
}

int *
metrics_setup_sockets(struct global_params *gp)
{
	struct addrinfo hints, *res, *res0;
	int error, nsock, *s;
	struct host_port hp;

	if (metrics_init_host_port(gp->c_metrics_port, &hp) == -1) {
		fprintf(stderr, "metrics_setup_listener: failed: '%s'\n",
		    gp->c_metrics_port);
		fprintf(stderr,
		    "--metrics-listen arg must be host:port format\n");
		exit(1);
	}
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = gp->c_family;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	error = getaddrinfo(hp.host, hp.port, &hints, &res0);
	if (error) {
		fprintf(stderr, "getaddrinfo: failed: %s\n",
		    gai_strerror(error));
		exit(1);
	}
	s = calloc(max_socks, sizeof(*s));
	if (s == NULL) {
		fprintf(stderr, "calloc failed: %s\n", strerror(errno));
		exit(1);
	}
	memset(s, -1, max_socks * sizeof(*s));
	nsock = 0;
	for (res = res0; res && nsock < max_socks; res = res->ai_next) {
		s[nsock] = socket(res->ai_family, res->ai_socktype,
		    res->ai_protocol);
		if (s[nsock] < 0) {
			fprintf(stderr, "socket failed: %s\n", strerror(errno));
			exit(1);
		}
		if (bind(s[nsock], res->ai_addr, res->ai_addrlen) < 0) {
			fprintf(stderr, "bind failed: %s\n", strerror(errno));
			exit(1);
		}
		(void) listen(s[nsock], 5);
		nsock++;
	}
	freeaddrinfo(res0);
	return (s);
}

void
metrics_http_must_write(int sock, char *data)
{
	ssize_t cc;

	while (1) {
		cc = send(sock, data, strlen(data), 0);
		if (cc == -1 && errno == EINTR) {
			continue;
		} else if (cc == -1) {
			fprintf(stderr, "send(2) failed: %s\n",
			    strerror(errno));
			exit(1);
		}
		return;
	}
}

void
metrics_http_ok(int sock, char *data)
{
	size_t clen;
	char hdr[256];
	char http_resp_hdr[] =
	    "HTTP/1.1 200 OK\r\n"
	    "Content-Type: text/plain\r\n"
	    "Connection: close\r\n";

	clen = strlen(data);
	snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", clen);
	metrics_http_must_write(sock, http_resp_hdr);
	metrics_http_must_write(sock, hdr);
	metrics_http_must_write(sock, data);
}

int
metrics_read_http_req(int sock)
{
	size_t method_len, path_len, num_headers, prevbuflen;
	struct phr_header headers[64];
	const char *method, *path;
	int pret, minor_version;
	char buf[4096];
	ssize_t cc;

	while (1) {
		cc = recv(sock, &buf[0], sizeof(buf), 0);
		if (cc == -1 && errno == EINTR) {
			continue;
		} else if (cc == -1) {
			return (-1);
		}
		break;
	}
	prevbuflen = 0;
	num_headers = sizeof(headers) / sizeof(headers[0]);
	pret = phr_parse_request(buf, cc, &method, &method_len, &path, &path_len,
		&minor_version, headers, &num_headers, prevbuflen);
	if (pret == -1) {
		fprintf(stderr, "phr_parse_request: failed to parse http req\n");
		return (-1);
	}
	// fprintf(stdout, "path is %.*s\n", (int)path_len, path);
	metrics_http_ok(sock, "OK");
	return (0);
}

int
metrics_evloop(int *socks)
{
	struct sockaddr_storage saddr;
	int error, j, maxfd, nsock;
	socklen_t slen;
	fd_set rfds;

	FD_ZERO(&rfds);
	maxfd = 0;
	for (j = 0; j < max_socks; j++) {
		if (socks[j] == -1) {
			break;
		}
		FD_SET(socks[j], &rfds);
		if (socks[j] > maxfd) {
			maxfd = socks[j];
		}
	}
	error = select(maxfd + 1, &rfds, NULL, NULL, NULL);
	if (error == -1 && errno == EINTR) {
		return (0);
	} else if (error == -1) {
		return (-1);
	}
	slen = sizeof(saddr);
	for (j = 0; j < max_socks; j++) {
		if (socks[j] == -1) {
			break;
		}
		if (!FD_ISSET(socks[j], &rfds)) {
			continue;
		}
		nsock = accept(socks[j], (struct sockaddr *)&saddr, &slen);
		if (nsock == -1 && errno == EINTR) {
			continue;
		}
		metrics_read_http_req(nsock);
		close(nsock);
	}
	return (0);
}

void *
metrics_launch(void *arg)
{
	struct global_params *gp;
	int *sockets;

	gp = (struct global_params *)arg;
	sockets = metrics_setup_sockets(gp);
	if (sockets == NULL) {
		fprintf(stderr, "failed to get metrics sockets\n");
		return (NULL);
	}
	while (1) { (void) metrics_evloop(sockets); }
	return (NULL);
}

int
metrics_setup_listener(struct global_params *gp)
{
	pthread_t thr;

	if (pthread_create(&thr, NULL, metrics_launch, gp) != 0) {
		fprintf(stderr, "pthread_create: failed: %s\n",
		    strerror(errno));
		return (-1);
	}
	return (0);
}
