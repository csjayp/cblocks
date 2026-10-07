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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <err.h>

#include <cblock/libcblock.h>

/*
 * Wire encoding for every message exchanged between cblock and cblockd.
 * Messages are encoded field by field so the client and daemon do not need
 * to agree on structure layout, MAXPATHLEN or the size of native types.
 */

typedef void	(*proto_enc_t)(struct wire *, const void *);
typedef void	(*proto_dec_t)(struct wire *, void *);

static int
proto_send(int sock, proto_enc_t enc, const void *arg)
{
	struct wire w;
	int ret;

	wire_init(&w);
	(*enc)(&w, arg);
	ret = wire_send(sock, &w);
	wire_free(&w);
	return (ret);
}

static int
proto_recv(int sock, proto_dec_t dec, void *arg)
{
	struct wire w;
	int ret;

	if (wire_recv(sock, &w) == -1) {
		return (-1);
	}
	(*dec)(&w, arg);
	ret = wire_finish(&w);
	wire_free(&w);
	return (ret);
}

int
proto_hello_client(int sock, char *errbuf, size_t len)
{
	uint32_t status, version;

	if (sock_ipc_write_u32(sock, CBLOCK_PROTO_MAGIC) == -1 ||
	    sock_ipc_write_u32(sock, CBLOCK_PROTO_VERSION) == -1) {
		snprintf(errbuf, len, "failed to send protocol hello");
		return (-1);
	}
	if (sock_ipc_read_u32(sock, &status) == -1 ||
	    sock_ipc_read_u32(sock, &version) == -1) {
		snprintf(errbuf, len, "daemon closed connection during hello");
		return (-1);
	}
	if (status != 0) {
		snprintf(errbuf, len,
		    "protocol version mismatch: client %u daemon %u",
		    CBLOCK_PROTO_VERSION, version);
		return (-1);
	}
	return (0);
}

int
proto_hello_server(int sock)
{
	uint32_t magic, version, status;

	if (sock_ipc_read_u32(sock, &magic) == -1) {
		return (-1);
	}
	if (magic != CBLOCK_PROTO_MAGIC) {
		warnx("bad protocol magic 0x%08x", magic);
		return (-1);
	}
	if (sock_ipc_read_u32(sock, &version) == -1) {
		return (-1);
	}
	status = (version == CBLOCK_PROTO_VERSION) ? 0 : 1;
	if (sock_ipc_write_u32(sock, status) == -1 ||
	    sock_ipc_write_u32(sock, CBLOCK_PROTO_VERSION) == -1) {
		return (-1);
	}
	if (status != 0) {
		warnx("client protocol version %u, expected %u", version,
		    CBLOCK_PROTO_VERSION);
		return (-1);
	}
	return (0);
}

static void
response_enc(struct wire *w, const void *arg)
{
	const struct cblock_response *r = arg;

	wire_put_u32(w, (uint32_t)r->p_ecode);
	wire_put_str(w, r->p_errbuf);
}

static void
response_dec(struct wire *w, void *arg)
{
	struct cblock_response *r = arg;

	r->p_ecode = (int32_t)wire_get_u32(w);
	wire_get_str(w, r->p_errbuf, sizeof(r->p_errbuf));
}

int
proto_send_response(int sock, const struct cblock_response *r)
{

	return (proto_send(sock, response_enc, r));
}

int
proto_recv_response(int sock, struct cblock_response *r)
{

	bzero(r, sizeof(*r));
	return (proto_recv(sock, response_dec, r));
}

static void
launch_enc(struct wire *w, const void *arg)
{
	const struct cblock_launch *l = arg;

	wire_put_str(w, l->p_name);
	wire_put_str(w, l->p_tag);
	wire_put_str(w, l->p_term);
	wire_put_str(w, l->p_entry_point_args);
	wire_put_str(w, l->p_volumes);
	wire_put_str(w, l->p_ports);
	wire_put_str(w, l->p_network);
	wire_put_u32(w, l->p_verbose);
}

static void
launch_dec(struct wire *w, void *arg)
{
	struct cblock_launch *l = arg;

	wire_get_str(w, l->p_name, sizeof(l->p_name));
	wire_get_str(w, l->p_tag, sizeof(l->p_tag));
	wire_get_str(w, l->p_term, sizeof(l->p_term));
	wire_get_str(w, l->p_entry_point_args, sizeof(l->p_entry_point_args));
	wire_get_str(w, l->p_volumes, sizeof(l->p_volumes));
	wire_get_str(w, l->p_ports, sizeof(l->p_ports));
	wire_get_str(w, l->p_network, sizeof(l->p_network));
	l->p_verbose = wire_get_int(w);
}

int
proto_send_launch(int sock, const struct cblock_launch *l)
{

	return (proto_send(sock, launch_enc, l));
}

int
proto_recv_launch(int sock, struct cblock_launch *l)
{

	bzero(l, sizeof(*l));
	return (proto_recv(sock, launch_dec, l));
}

static void
signal_enc(struct wire *w, const void *arg)
{
	const struct cblock_signal_instance *s = arg;

	wire_put_str(w, s->p_instance);
	wire_put_u32(w, s->p_sig);
}

static void
signal_dec(struct wire *w, void *arg)
{
	struct cblock_signal_instance *s = arg;

	wire_get_str(w, s->p_instance, sizeof(s->p_instance));
	s->p_sig = wire_get_int(w);
}

int
proto_send_signal(int sock, const struct cblock_signal_instance *s)
{

	return (proto_send(sock, signal_enc, s));
}

int
proto_recv_signal(int sock, struct cblock_signal_instance *s)
{

	bzero(s, sizeof(*s));
	return (proto_recv(sock, signal_dec, s));
}

static void
winsize_enc(struct wire *w, const struct winsize *ws)
{

	wire_put_u16(w, ws->ws_row);
	wire_put_u16(w, ws->ws_col);
	wire_put_u16(w, ws->ws_xpixel);
	wire_put_u16(w, ws->ws_ypixel);
}

static void
winsize_dec(struct wire *w, struct winsize *ws)
{

	ws->ws_row = wire_get_u16(w);
	ws->ws_col = wire_get_u16(w);
	ws->ws_xpixel = wire_get_u16(w);
	ws->ws_ypixel = wire_get_u16(w);
}

static void
winsize_enc_cb(struct wire *w, const void *arg)
{

	winsize_enc(w, arg);
}

static void
winsize_dec_cb(struct wire *w, void *arg)
{

	winsize_dec(w, arg);
}

int
proto_send_winsize(int sock, const struct winsize *ws)
{

	return (proto_send(sock, winsize_enc_cb, ws));
}

int
proto_recv_winsize(int sock, struct winsize *ws)
{

	bzero(ws, sizeof(*ws));
	return (proto_recv(sock, winsize_dec_cb, ws));
}

static void
console_connect_enc(struct wire *w, const void *arg)
{
	const struct cblock_console_connect *c = arg;

	wire_put_str(w, c->p_name);
	wire_put_str(w, c->p_instance);
	wire_put_str(w, c->p_term);
	winsize_enc(w, &c->p_winsize);
}

static void
console_connect_dec(struct wire *w, void *arg)
{
	struct cblock_console_connect *c = arg;

	wire_get_str(w, c->p_name, sizeof(c->p_name));
	wire_get_str(w, c->p_instance, sizeof(c->p_instance));
	wire_get_str(w, c->p_term, sizeof(c->p_term));
	winsize_dec(w, &c->p_winsize);
}

int
proto_send_console_connect(int sock, const struct cblock_console_connect *c)
{

	return (proto_send(sock, console_connect_enc, c));
}

int
proto_recv_console_connect(int sock, struct cblock_console_connect *c)
{

	bzero(c, sizeof(*c));
	return (proto_recv(sock, console_connect_dec, c));
}

/*
 * The marshalled argument vector (if any) travels in the same frame as the
 * command. p_mlen describes its length.
 */
int
proto_send_generic_command(int sock, const struct cblock_generic_command *g,
    const char *payload)
{
	struct wire w;
	int ret;

	wire_init(&w);
	wire_put_str(&w, g->p_cmdname);
	wire_put_u32(&w, g->p_verbose);
	wire_put_blob(&w, payload, payload != NULL ? g->p_mlen : 0);
	ret = wire_send(sock, &w);
	wire_free(&w);
	return (ret);
}

int
proto_recv_generic_command(int sock, struct cblock_generic_command *g,
    char **payload)
{
	const void *data;
	struct wire w;
	size_t len;
	int ret;

	bzero(g, sizeof(*g));
	*payload = NULL;
	if (wire_recv(sock, &w) == -1) {
		return (-1);
	}
	wire_get_str(&w, g->p_cmdname, sizeof(g->p_cmdname));
	g->p_verbose = wire_get_int(&w);
	data = wire_get_blob(&w, &len);
	ret = wire_finish(&w);
	/*
	 * vec_unmarshal() relies on the payload being NUL terminated.
	 */
	if (ret == 0 && len > 0 && ((const char *)data)[len - 1] != '\0') {
		ret = -1;
	}
	if (ret == 0 && len > 0) {
		*payload = malloc(len);
		if (*payload == NULL) {
			ret = -1;
		} else {
			memcpy(*payload, data, len);
			g->p_mlen = len;
		}
	}
	wire_free(&w);
	return (ret);
}

static void
build_context_enc(struct wire *w, const void *arg)
{
	const struct cblock_build_context *b = arg;

	wire_put_str(w, b->p_image_name);
	wire_put_str(w, b->p_cblock_file);
	wire_put_u64(w, b->p_context_size);
	wire_put_str(w, b->p_tag);
	wire_put_u32(w, b->p_nstages);
	wire_put_u32(w, b->p_nsteps);
	wire_put_str(w, b->p_term);
	wire_put_str(w, b->p_entry_point);
	wire_put_str(w, b->p_entry_point_args);
	wire_put_u32(w, b->p_verbose);
	wire_put_u32(w, b->p_build_fim_spec);
	wire_put_str(w, b->p_os_release);
	wire_put_str(w, b->p_auditcfg);
}

static void
build_context_dec(struct wire *w, void *arg)
{
	struct cblock_build_context *b = arg;
	uint64_t size;

	wire_get_str(w, b->p_image_name, sizeof(b->p_image_name));
	wire_get_str(w, b->p_cblock_file, sizeof(b->p_cblock_file));
	size = wire_get_u64(w);
	if (size > INT64_MAX) {
		w->w_error = 1;
	}
	b->p_context_size = size;
	wire_get_str(w, b->p_tag, sizeof(b->p_tag));
	b->p_nstages = wire_get_int(w);
	b->p_nsteps = wire_get_int(w);
	wire_get_str(w, b->p_term, sizeof(b->p_term));
	wire_get_str(w, b->p_entry_point, sizeof(b->p_entry_point));
	wire_get_str(w, b->p_entry_point_args, sizeof(b->p_entry_point_args));
	b->p_verbose = wire_get_int(w);
	b->p_build_fim_spec = wire_get_int(w);
	wire_get_str(w, b->p_os_release, sizeof(b->p_os_release));
	wire_get_str(w, b->p_auditcfg, sizeof(b->p_auditcfg));
}

int
proto_send_build_context(int sock, const struct cblock_build_context *b)
{

	return (proto_send(sock, build_context_enc, b));
}

int
proto_recv_build_context(int sock, struct cblock_build_context *b)
{

	bzero(b, sizeof(*b));
	return (proto_recv(sock, build_context_dec, b));
}

static void
build_stage_enc(struct wire *w, const void *arg)
{
	const struct build_stage *s = arg;

	wire_put_str(w, s->bs_name);
	wire_put_u32(w, s->bs_index);
	wire_put_str(w, s->bs_base_container);
	wire_put_u32(w, s->bs_is_last);
}

static void
build_stage_dec(struct wire *w, void *arg)
{
	struct build_stage *s = arg;

	wire_get_str(w, s->bs_name, sizeof(s->bs_name));
	s->bs_index = wire_get_int(w);
	wire_get_str(w, s->bs_base_container, sizeof(s->bs_base_container));
	s->bs_is_last = wire_get_int(w);
}

int
proto_send_build_stage(int sock, const struct build_stage *s)
{

	return (proto_send(sock, build_stage_enc, s));
}

int
proto_recv_build_stage(int sock, struct build_stage *s)
{

	bzero(s, sizeof(*s));
	return (proto_recv(sock, build_stage_dec, s));
}

/*
 * Only the union member that corresponds to step_op is transmitted.
 */
static void
build_step_enc(struct wire *w, const void *arg)
{
	const struct build_step *s = arg;

	wire_put_u32(w, s->step_op);
	wire_put_u32(w, s->stage_index);
	wire_put_str(w, s->step_string);
	switch (s->step_op) {
	case STEP_ADD:
		wire_put_u32(w, s->step_data.step_add.sa_op);
		wire_put_str(w, s->step_data.step_add.sa_source);
		wire_put_str(w, s->step_data.step_add.sa_dest);
		break;
	case STEP_COPY:
		wire_put_str(w, s->step_data.step_copy.sc_source);
		wire_put_str(w, s->step_data.step_copy.sc_dest);
		break;
	case STEP_RUN:
		wire_put_str(w, s->step_data.step_cmd);
		break;
	case STEP_WORKDIR:
		wire_put_str(w, s->step_data.step_workdir.sw_dir);
		break;
	case STEP_COPY_FROM:
		wire_put_u32(w, s->step_data.step_copy_from.sc_stage);
		wire_put_str(w, s->step_data.step_copy_from.sc_source);
		wire_put_str(w, s->step_data.step_copy_from.sc_dest);
		break;
	case STEP_ROOT_PIVOT:
		wire_put_str(w, s->step_data.step_root_pivot.sr_dir);
		break;
	case STEP_ENV:
		wire_put_str(w, s->step_data.step_env.se_key);
		wire_put_str(w, s->step_data.step_env.se_value);
		break;
	default:
		w->w_error = 1;
	}
}

static void
build_step_dec(struct wire *w, void *arg)
{
	struct build_step *s = arg;

	s->step_op = wire_get_int(w);
	s->stage_index = wire_get_int(w);
	wire_get_str(w, s->step_string, sizeof(s->step_string));
	switch (s->step_op) {
	case STEP_ADD:
		s->step_data.step_add.sa_op = wire_get_int(w);
		wire_get_str(w, s->step_data.step_add.sa_source,
		    sizeof(s->step_data.step_add.sa_source));
		wire_get_str(w, s->step_data.step_add.sa_dest,
		    sizeof(s->step_data.step_add.sa_dest));
		break;
	case STEP_COPY:
		wire_get_str(w, s->step_data.step_copy.sc_source,
		    sizeof(s->step_data.step_copy.sc_source));
		wire_get_str(w, s->step_data.step_copy.sc_dest,
		    sizeof(s->step_data.step_copy.sc_dest));
		break;
	case STEP_RUN:
		wire_get_str(w, s->step_data.step_cmd,
		    sizeof(s->step_data.step_cmd));
		break;
	case STEP_WORKDIR:
		wire_get_str(w, s->step_data.step_workdir.sw_dir,
		    sizeof(s->step_data.step_workdir.sw_dir));
		break;
	case STEP_COPY_FROM:
		s->step_data.step_copy_from.sc_stage = wire_get_int(w);
		wire_get_str(w, s->step_data.step_copy_from.sc_source,
		    sizeof(s->step_data.step_copy_from.sc_source));
		wire_get_str(w, s->step_data.step_copy_from.sc_dest,
		    sizeof(s->step_data.step_copy_from.sc_dest));
		break;
	case STEP_ROOT_PIVOT:
		wire_get_str(w, s->step_data.step_root_pivot.sr_dir,
		    sizeof(s->step_data.step_root_pivot.sr_dir));
		break;
	case STEP_ENV:
		wire_get_str(w, s->step_data.step_env.se_key,
		    sizeof(s->step_data.step_env.se_key));
		wire_get_str(w, s->step_data.step_env.se_value,
		    sizeof(s->step_data.step_env.se_value));
		break;
	default:
		w->w_error = 1;
	}
}

int
proto_send_build_step(int sock, const struct build_step *s)
{

	return (proto_send(sock, build_step_enc, s));
}

int
proto_recv_build_step(int sock, struct build_step *s)
{

	bzero(s, sizeof(*s));
	return (proto_recv(sock, build_step_dec, s));
}

int
proto_send_instances(int sock, const struct instance_ent *ents, size_t count)
{
	const struct instance_ent *e;
	struct wire w;
	size_t k;
	int ret;

	wire_init(&w);
	wire_put_u32(&w, count);
	for (k = 0; k < count; k++) {
		e = &ents[k];
		wire_put_str(&w, e->p_instance_name);
		wire_put_str(&w, e->p_image_name);
		wire_put_u32(&w, e->p_pid);
		wire_put_str(&w, e->p_tty_line);
		wire_put_u64(&w, e->p_start_time);
		wire_put_str(&w, e->p_type);
	}
	ret = wire_send(sock, &w);
	wire_free(&w);
	return (ret);
}

/*
 * On success *ents is allocated (or NULL if there are no instances) and must
 * be freed by the caller.
 */
int
proto_recv_instances(int sock, struct instance_ent **ents, size_t *count)
{
	struct instance_ent *vec, *e;
	struct wire w;
	uint32_t n, k;

	*ents = NULL;
	*count = 0;
	if (wire_recv(sock, &w) == -1) {
		return (-1);
	}
	n = wire_get_u32(&w);
	/*
	 * Each entry takes at least 28 bytes on the wire, use that to bound
	 * the allocation before trusting the count.
	 */
	if (w.w_error || n > (w.w_len - w.w_off) / 28) {
		wire_free(&w);
		return (-1);
	}
	vec = NULL;
	if (n > 0) {
		vec = calloc(n, sizeof(*vec));
		if (vec == NULL) {
			wire_free(&w);
			return (-1);
		}
	}
	for (k = 0; k < n; k++) {
		e = &vec[k];
		wire_get_str(&w, e->p_instance_name,
		    sizeof(e->p_instance_name));
		wire_get_str(&w, e->p_image_name, sizeof(e->p_image_name));
		e->p_pid = wire_get_u32(&w);
		wire_get_str(&w, e->p_tty_line, sizeof(e->p_tty_line));
		e->p_start_time = wire_get_u64(&w);
		wire_get_str(&w, e->p_type, sizeof(e->p_type));
	}
	if (wire_finish(&w) == -1) {
		free(vec);
		wire_free(&w);
		return (-1);
	}
	wire_free(&w);
	*ents = vec;
	*count = n;
	return (0);
}
