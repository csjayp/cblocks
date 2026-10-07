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

#include <arpa/inet.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <cblock/libcblock.h>

void
wire_init(struct wire *w)
{

	bzero(w, sizeof(*w));
}

void
wire_free(struct wire *w)
{

	free(w->w_buf);
	bzero(w, sizeof(*w));
}

static int
wire_reserve(struct wire *w, size_t len)
{
	size_t newcap;
	u_char *p;

	if (w->w_error) {
		return (-1);
	}
	if (len > CBLOCK_WIRE_MAX - w->w_len) {
		w->w_error = 1;
		return (-1);
	}
	if (w->w_len + len <= w->w_cap) {
		return (0);
	}
	newcap = w->w_cap ? w->w_cap : 256;
	while (newcap < w->w_len + len) {
		newcap *= 2;
	}
	p = realloc(w->w_buf, newcap);
	if (p == NULL) {
		w->w_error = 1;
		return (-1);
	}
	w->w_buf = p;
	w->w_cap = newcap;
	return (0);
}

void
wire_put_bytes(struct wire *w, const void *data, size_t len)
{

	if (wire_reserve(w, len) == -1 || len == 0) {
		return;
	}
	memcpy(w->w_buf + w->w_len, data, len);
	w->w_len += len;
}

void
wire_put_u16(struct wire *w, uint16_t v)
{

	v = htons(v);
	wire_put_bytes(w, &v, sizeof(v));
}

void
wire_put_u32(struct wire *w, uint32_t v)
{

	v = htonl(v);
	wire_put_bytes(w, &v, sizeof(v));
}

/*
 * There is no portable htonll(), so send the high and low words separately.
 */
void
wire_put_u64(struct wire *w, uint64_t v)
{

	wire_put_u32(w, v >> 32);
	wire_put_u32(w, v & 0xffffffff);
}

void
wire_put_blob(struct wire *w, const void *data, size_t len)
{

	if (len > UINT32_MAX) {
		w->w_error = 1;
		return;
	}
	wire_put_u32(w, len);
	wire_put_bytes(w, data, len);
}

void
wire_put_str(struct wire *w, const char *s)
{

	if (s == NULL) {
		s = "";
	}
	wire_put_blob(w, s, strlen(s));
}

static const u_char *
wire_take(struct wire *w, size_t len)
{
	const u_char *p;

	if (w->w_error || len > w->w_len - w->w_off) {
		w->w_error = 1;
		return (NULL);
	}
	p = w->w_buf + w->w_off;
	w->w_off += len;
	return (p);
}

uint16_t
wire_get_u16(struct wire *w)
{
	const u_char *p;
	uint16_t v;

	p = wire_take(w, sizeof(v));
	if (p == NULL) {
		return (0);
	}
	memcpy(&v, p, sizeof(v));
	return (ntohs(v));
}

uint32_t
wire_get_u32(struct wire *w)
{
	const u_char *p;
	uint32_t v;

	p = wire_take(w, sizeof(v));
	if (p == NULL) {
		return (0);
	}
	memcpy(&v, p, sizeof(v));
	return (ntohl(v));
}

uint64_t
wire_get_u64(struct wire *w)
{
	uint64_t v;

	v = (uint64_t)wire_get_u32(w) << 32;
	v |= wire_get_u32(w);
	return (v);
}

/*
 * Decode a non-negative int. Anything that does not fit is an error rather
 * than being silently turned into a negative number.
 */
int
wire_get_int(struct wire *w)
{
	uint32_t v;

	v = wire_get_u32(w);
	if (v > INT_MAX) {
		w->w_error = 1;
		return (0);
	}
	return (v);
}

const void *
wire_get_blob(struct wire *w, size_t *len)
{
	const u_char *p;
	uint32_t n;

	*len = 0;
	n = wire_get_u32(w);
	p = wire_take(w, n);
	if (p == NULL) {
		return (NULL);
	}
	*len = n;
	return (p);
}

/*
 * Copy a string into a fixed size buffer. Strings which do not fit, or which
 * contain embedded NUL bytes are rejected, so the result is always a valid
 * NUL terminated string.
 */
void
wire_get_str(struct wire *w, char *dst, size_t dstlen)
{
	const char *p;
	size_t len;

	if (dstlen > 0) {
		dst[0] = '\0';
	}
	p = wire_get_blob(w, &len);
	if (p == NULL) {
		return;
	}
	if (len >= dstlen || memchr(p, '\0', len) != NULL) {
		w->w_error = 1;
		return;
	}
	memcpy(dst, p, len);
	dst[len] = '\0';
}

/*
 * Returns 0 if the entire message was decoded without error.
 */
int
wire_finish(struct wire *w)
{

	if (w->w_error || w->w_off != w->w_len) {
		return (-1);
	}
	return (0);
}

int
sock_ipc_write_u32(int sock, uint32_t v)
{

	v = htonl(v);
	if (sock_ipc_must_write(sock, &v, sizeof(v)) != sizeof(v)) {
		return (-1);
	}
	return (0);
}

/*
 * Returns -1 on EOF.
 */
int
sock_ipc_read_u32(int sock, uint32_t *v)
{
	uint32_t nv;

	if (sock_ipc_must_read(sock, &nv, sizeof(nv)) != sizeof(nv)) {
		return (-1);
	}
	*v = ntohl(nv);
	return (0);
}

/*
 * Frames are a 32-bit length followed by the message bytes.
 */
int
wire_send(int sock, struct wire *w)
{

	if (w->w_error) {
		return (-1);
	}
	if (sock_ipc_write_u32(sock, w->w_len) == -1) {
		return (-1);
	}
	if (w->w_len == 0) {
		return (0);
	}
	if (sock_ipc_must_write(sock, w->w_buf, w->w_len) !=
	    (ssize_t)w->w_len) {
		return (-1);
	}
	return (0);
}

int
wire_recv(int sock, struct wire *w)
{
	uint32_t len;

	wire_init(w);
	if (sock_ipc_read_u32(sock, &len) == -1) {
		return (-1);
	}
	if (len > CBLOCK_WIRE_MAX) {
		warnx("wire: frame length %u exceeds maximum", len);
		return (-1);
	}
	if (len == 0) {
		return (0);
	}
	w->w_buf = malloc(len);
	if (w->w_buf == NULL) {
		return (-1);
	}
	w->w_cap = len;
	if (sock_ipc_must_read(sock, w->w_buf, len) != len) {
		wire_free(w);
		return (-1);
	}
	w->w_len = len;
	return (0);
}

/*
 * Send a command header followed by a single frame of raw bytes. Used for
 * console data in both directions, avoiding a copy into a wire buffer.
 */
int
wire_send_frame(int sock, uint32_t cmd, const void *data, size_t len)
{

	if (len > CBLOCK_WIRE_MAX) {
		return (-1);
	}
	if (sock_ipc_write_u32(sock, cmd) == -1 ||
	    sock_ipc_write_u32(sock, len) == -1) {
		return (-1);
	}
	if (len == 0) {
		return (0);
	}
	if (sock_ipc_must_write(sock, (void *)(uintptr_t)data, len) !=
	    (ssize_t)len) {
		return (-1);
	}
	return (0);
}
