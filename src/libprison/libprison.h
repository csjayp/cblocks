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
#ifndef LIBPRISON_DOT_H_
#define	LIBPRISON_DOT_H_

int		sock_ipc_may_read(int, void *, size_t);
ssize_t		sock_ipc_must_read(int, void *, size_t);
ssize_t		sock_ipc_must_write(int, void *, size_t);

#define	MAX_PRISON_NAME	512

#define	PRISON_IPC_LAUNCH_PRISON	1
#define	PRISON_IPC_CONSOLE_CONNECT	2
#define	PRISON_IPC_CONSOLE_DATA		3
#define PRISON_IPC_CONSOL_RESIZE	4
#define	PRISON_IPC_SEND_BUILD_CTX	5

struct prison_build_context {
	char		p_image_name[1024];
	char		p_prison_file[1024];
	off_t		p_context_size;
};

struct prison_response {
	int		p_ecode;
	char		p_errbuf[512];
};

struct prison_launch {
	char		p_name[MAX_PRISON_NAME];
	char		p_term[128];
};

struct prison_console_connect {
	char		p_name[MAX_PRISON_NAME];
	struct winsize 	p_winsize;
	struct termios	p_termios;
	char		p_term[64];
};

#endif
