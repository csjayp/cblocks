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
#ifndef DISPATCH_DOT_H_
#define DISPATCH_DOT_H_

struct cblock_instance {
        int                             p_type;
        uint32_t                        p_state;
#define STATE_DEAD              0x00000001
#define STATE_CONNECTED         0x00000002
        char                            p_name[256];
        pid_t                           p_pid;
        int                             p_ttyfd;
        char                            p_ttyname[256];
        TAILQ_ENTRY(cblock_instance)    p_glue;
        struct tty_buffer               p_ttybuf;
        int                             p_peer_sock;
        int                             p_pipe[2];
        char                            *p_instance_tag;
        time_t                          p_launch_time;
	char				p_image_name[256];
	int				p_pid_file;
};
typedef TAILQ_HEAD( , cblock_peer) cblock_peer_head_t;
typedef TAILQ_HEAD( , cblock_instance) cblock_instance_head_t;

int		dispatch_get_instances(int);
int		dispatch_generic_command(int);
void *		tty_io_queue_loop(void *);
int		dispatch_build_recieve(int);
char *		gen_sha256_instance_id(char *instance_name);
void		cblock_fork_cleanup(char *instance, char *, int, int);
void		tty_handle_resize(int, char *);
void		tty_console_session(const char *, int, int);
char *		tty_trim_buffer(char *, size_t, size_t *);
void		gen_sha256_string(unsigned char *, char *);
char *		gen_sha256_instance_id(char *);
void *		dispatch_work(void *);

cblock_peer_head_t p_head;
cblock_instance_head_t pr_head;
pthread_mutex_t peer_mutex;
pthread_mutex_t cblock_mutex;

#endif
